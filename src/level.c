/*
 * Sector-based level builder.
 *
 * Generates a single watertight mesh from 2D sector definitions.
 * Handles partial edge overlaps: a narrow corridor connecting to a
 * wide room generates wall segments on either side of the doorway.
 */

#include "sdl3d/level.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "sdl3d/math.h"
#include "sdl3d/properties.h"
#include "sdl3d/signal_bus.h"

#define LM_TEXELS_PER_UNIT 4
#define LM_MIN_SURFACE_TEXELS 2
#define LM_MIN_ATLAS_SIZE 256
#define LM_MAX_ATLAS_SIZE 2048
#define SDL3D_LEVEL_PLANE_EPSILON 0.000001f

/* ------------------------------------------------------------------ */
/* Edge overlap detection                                              */
/* ------------------------------------------------------------------ */

typedef struct
{
    float ax, az, bx, bz;
    int sector;
} edge_info;

static bool edges_collinear(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz)
{
    float eps = 0.01f;
    float cross1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
    float cross2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    return SDL_fabsf(cross1) < eps && SDL_fabsf(cross2) < eps;
}

static float project_onto_edge(float px, float pz, float ax, float az, float bx, float bz)
{
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    if (len2 < 0.0001f)
        return -1;
    return ((px - ax) * dx + (pz - az) * dz) / len2;
}

typedef struct
{
    float t0, t1;
    int other_sector;
} overlap_t;

/* ------------------------------------------------------------------ */
/* Sector planes                                                       */
/* ------------------------------------------------------------------ */

static sdl3d_vec3 normalized_or_default(const float normal[3], sdl3d_vec3 fallback)
{
    if (normal == NULL)
    {
        return fallback;
    }

    const float len = SDL_sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (len <= SDL3D_LEVEL_PLANE_EPSILON)
    {
        return fallback;
    }

    const float inv_len = 1.0f / len;
    return sdl3d_vec3_make(normal[0] * inv_len, normal[1] * inv_len, normal[2] * inv_len);
}

sdl3d_vec3 sdl3d_sector_floor_normal(const sdl3d_sector *sector)
{
    return sector != NULL ? normalized_or_default(sector->floor_normal, sdl3d_vec3_make(0.0f, 1.0f, 0.0f))
                          : sdl3d_vec3_make(0.0f, 1.0f, 0.0f);
}

sdl3d_vec3 sdl3d_sector_ceil_normal(const sdl3d_sector *sector)
{
    return sector != NULL ? normalized_or_default(sector->ceil_normal, sdl3d_vec3_make(0.0f, -1.0f, 0.0f))
                          : sdl3d_vec3_make(0.0f, -1.0f, 0.0f);
}

sdl3d_vec3 sdl3d_sector_push_velocity(const sdl3d_sector *sector)
{
    if (sector == NULL)
    {
        return sdl3d_vec3_make(0.0f, 0.0f, 0.0f);
    }
    return sdl3d_vec3_make(sector->push_velocity[0], sector->push_velocity[1], sector->push_velocity[2]);
}

float sdl3d_sector_damage_per_second(const sdl3d_sector *sector)
{
    if (sector == NULL || sector->damage_per_second <= 0.0f)
    {
        return 0.0f;
    }
    return sector->damage_per_second;
}

float sdl3d_sector_damage_for_delta(const sdl3d_sector *sector, float dt)
{
    if (dt <= 0.0f)
    {
        return 0.0f;
    }
    return sdl3d_sector_damage_per_second(sector) * dt;
}

static void sector_centroid_xz(const sdl3d_sector *sector, float *out_x, float *out_z)
{
    float x = 0.0f;
    float z = 0.0f;

    if (sector == NULL || sector->num_points <= 0)
    {
        *out_x = 0.0f;
        *out_z = 0.0f;
        return;
    }

    for (int i = 0; i < sector->num_points; ++i)
    {
        x += sector->points[i][0];
        z += sector->points[i][1];
    }

    const float inv_count = 1.0f / (float)sector->num_points;
    *out_x = x * inv_count;
    *out_z = z * inv_count;
}

static float sector_plane_y_at(const sdl3d_sector *sector, sdl3d_vec3 normal, float base_y, float x, float z)
{
    float cx;
    float cz;

    if (sector == NULL || SDL_fabsf(normal.y) <= SDL3D_LEVEL_PLANE_EPSILON)
    {
        return base_y;
    }

    sector_centroid_xz(sector, &cx, &cz);
    return base_y - (normal.x * (x - cx) + normal.z * (z - cz)) / normal.y;
}

float sdl3d_sector_floor_at(const sdl3d_sector *sector, float x, float z)
{
    if (sector == NULL)
    {
        return 0.0f;
    }
    return sector_plane_y_at(sector, sdl3d_sector_floor_normal(sector), sector->floor_y, x, z);
}

float sdl3d_sector_ceil_at(const sdl3d_sector *sector, float x, float z)
{
    if (sector == NULL)
    {
        return 0.0f;
    }
    return sector_plane_y_at(sector, sdl3d_sector_ceil_normal(sector), sector->ceil_y, x, z);
}

/* ------------------------------------------------------------------ */
/* Mesh accumulator                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    float *pos;
    float *nrm;
    float *col;
    float *uvs;
    float *lm_uvs;
    unsigned int *idx;
    int vc, vc_cap;
    int ic, ic_cap;
    bool has_bounds;
    sdl3d_bounding_box bounds;
} macc;

typedef enum
{
    LM_SURFACE_WALL = 0,
    LM_SURFACE_FLOOR = 1,
    LM_SURFACE_CEILING = 2
} lm_surface_type;

typedef struct
{
    int acc_index;
    int first_vertex;
    int vertex_count;
    lm_surface_type type;
    float nx, ny, nz;
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
    float x0, z0, x1, z1;
    float bot0, top0;
    float bot1, top1;
    float plane_cx, plane_cz;
    float plane_y;
    int atlas_x, atlas_y;
    int atlas_w, atlas_h;
} lm_surface;

typedef struct
{
    lm_surface *items;
    int count;
    int capacity;
} lm_surface_list;

static bool macc_grow_v(macc *a, int n)
{
    if (a->vc + n <= a->vc_cap)
        return true;
    int c = a->vc_cap * 2;
    if (c < a->vc + n)
        c = a->vc + n;
    float *p = SDL_realloc(a->pos, (size_t)c * 3 * sizeof(float));
    float *nr = SDL_realloc(a->nrm, (size_t)c * 3 * sizeof(float));
    float *co = SDL_realloc(a->col, (size_t)c * 4 * sizeof(float));
    float *uv = SDL_realloc(a->uvs, (size_t)c * 2 * sizeof(float));
    if (!p || !nr || !co || !uv)
        return false;
    a->pos = p;
    a->nrm = nr;
    a->col = co;
    a->uvs = uv;
    a->vc_cap = c;
    return true;
}

static bool macc_grow_i(macc *a, int n)
{
    if (a->ic + n <= a->ic_cap)
        return true;
    int c = a->ic_cap * 2;
    if (c < a->ic + n)
        c = a->ic + n;
    unsigned int *idx = SDL_realloc(a->idx, (size_t)c * sizeof(unsigned int));
    if (!idx)
        return false;
    a->idx = idx;
    a->ic_cap = c;
    return true;
}

static int macc_vert(macc *a, float x, float y, float z, float nx, float ny, float nz, const float *rgba, float u,
                     float v)
{
    if (!macc_grow_v(a, 1))
        return -1;
    int i = a->vc++;
    a->pos[i * 3] = x;
    a->pos[i * 3 + 1] = y;
    a->pos[i * 3 + 2] = z;
    a->nrm[i * 3] = nx;
    a->nrm[i * 3 + 1] = ny;
    a->nrm[i * 3 + 2] = nz;
    a->col[i * 4] = rgba[0];
    a->col[i * 4 + 1] = rgba[1];
    a->col[i * 4 + 2] = rgba[2];
    a->col[i * 4 + 3] = rgba[3];
    a->uvs[i * 2] = u;
    a->uvs[i * 2 + 1] = v;
    if (!a->has_bounds)
    {
        a->bounds.min.x = x;
        a->bounds.min.y = y;
        a->bounds.min.z = z;
        a->bounds.max.x = x;
        a->bounds.max.y = y;
        a->bounds.max.z = z;
        a->has_bounds = true;
    }
    else
    {
        if (x < a->bounds.min.x)
            a->bounds.min.x = x;
        if (y < a->bounds.min.y)
            a->bounds.min.y = y;
        if (z < a->bounds.min.z)
            a->bounds.min.z = z;
        if (x > a->bounds.max.x)
            a->bounds.max.x = x;
        if (y > a->bounds.max.y)
            a->bounds.max.y = y;
        if (z > a->bounds.max.z)
            a->bounds.max.z = z;
    }
    return i;
}

static bool macc_tri(macc *a, int a0, int a1, int a2)
{
    if (!macc_grow_i(a, 3))
        return false;
    a->idx[a->ic++] = (unsigned int)a0;
    a->idx[a->ic++] = (unsigned int)a1;
    a->idx[a->ic++] = (unsigned int)a2;
    return true;
}

static void macc_free(macc *a)
{
    SDL_free(a->pos);
    SDL_free(a->nrm);
    SDL_free(a->col);
    SDL_free(a->uvs);
    SDL_free(a->lm_uvs);
    SDL_free(a->idx);
}

static bool lm_surface_list_append(lm_surface_list *list, const lm_surface *surface)
{
    if (list->count == list->capacity)
    {
        int new_capacity = list->capacity > 0 ? list->capacity * 2 : 64;
        lm_surface *items = SDL_realloc(list->items, (size_t)new_capacity * sizeof(*items));
        if (items == NULL)
        {
            return SDL_OutOfMemory();
        }
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = *surface;
    return true;
}

static int lm_surface_texels(float units)
{
    int texels = (int)SDL_ceilf(units * (float)LM_TEXELS_PER_UNIT);
    return texels < LM_MIN_SURFACE_TEXELS ? LM_MIN_SURFACE_TEXELS : texels;
}

static void lm_sector_bounds_xz(const sdl3d_sector *sector, float *min_x, float *max_x, float *min_z, float *max_z)
{
    float sx0 = sector->points[0][0], sx1 = sector->points[0][0];
    float sz0 = sector->points[0][1], sz1 = sector->points[0][1];

    for (int i = 1; i < sector->num_points; ++i)
    {
        float x = sector->points[i][0];
        float z = sector->points[i][1];
        if (x < sx0)
            sx0 = x;
        if (x > sx1)
            sx1 = x;
        if (z < sz0)
            sz0 = z;
        if (z > sz1)
            sz1 = z;
    }

    *min_x = sx0;
    *max_x = sx1;
    *min_z = sz0;
    *max_z = sz1;
}

static float min4(float a, float b, float c, float d)
{
    float value = a < b ? a : b;
    value = value < c ? value : c;
    return value < d ? value : d;
}

static float max4(float a, float b, float c, float d)
{
    float value = a > b ? a : b;
    value = value > c ? value : c;
    return value > d ? value : d;
}

static bool lm_record_wall_surface(lm_surface_list *list, int acc_index, const macc *acc, int first_vertex, float x0,
                                   float z0, float x1, float z1, float bot0, float top0, float bot1, float top1)
{
    float dx = x1 - x0;
    float dz = z1 - z0;
    float len = SDL_sqrtf(dx * dx + dz * dz);
    float height0 = top0 - bot0;
    float height1 = top1 - bot1;
    lm_surface surface;

    if (acc->vc <= first_vertex || len < 0.0001f || (height0 < 0.001f && height1 < 0.001f))
    {
        return true;
    }

    SDL_zero(surface);
    surface.acc_index = acc_index;
    surface.first_vertex = first_vertex;
    surface.vertex_count = acc->vc - first_vertex;
    surface.type = LM_SURFACE_WALL;
    surface.nx = -dz / len;
    surface.ny = 0.0f;
    surface.nz = dx / len;
    surface.min_x = x0 < x1 ? x0 : x1;
    surface.max_x = x0 > x1 ? x0 : x1;
    surface.min_y = min4(bot0, top0, bot1, top1);
    surface.max_y = max4(bot0, top0, bot1, top1);
    surface.min_z = z0 < z1 ? z0 : z1;
    surface.max_z = z0 > z1 ? z0 : z1;
    surface.x0 = x0;
    surface.z0 = z0;
    surface.x1 = x1;
    surface.z1 = z1;
    surface.bot0 = bot0;
    surface.top0 = top0;
    surface.bot1 = bot1;
    surface.top1 = top1;
    surface.atlas_w = lm_surface_texels(len);
    surface.atlas_h = lm_surface_texels(surface.max_y - surface.min_y);
    return lm_surface_list_append(list, &surface);
}

static bool lm_record_floor_ceil_surface(lm_surface_list *list, int acc_index, const macc *acc, int first_vertex,
                                         const sdl3d_sector *sector, bool floor_surface)
{
    lm_surface surface;
    sdl3d_vec3 normal = floor_surface ? sdl3d_sector_floor_normal(sector) : sdl3d_sector_ceil_normal(sector);
    float min_y = 1e30f;
    float max_y = -1e30f;

    if (acc->vc <= first_vertex)
    {
        return true;
    }

    SDL_zero(surface);
    surface.acc_index = acc_index;
    surface.first_vertex = first_vertex;
    surface.vertex_count = acc->vc - first_vertex;
    surface.type = floor_surface ? LM_SURFACE_FLOOR : LM_SURFACE_CEILING;
    surface.nx = normal.x;
    surface.ny = normal.y;
    surface.nz = normal.z;
    lm_sector_bounds_xz(sector, &surface.min_x, &surface.max_x, &surface.min_z, &surface.max_z);
    for (int i = 0; i < sector->num_points; ++i)
    {
        float y = floor_surface ? sdl3d_sector_floor_at(sector, sector->points[i][0], sector->points[i][1])
                                : sdl3d_sector_ceil_at(sector, sector->points[i][0], sector->points[i][1]);
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
    }
    sector_centroid_xz(sector, &surface.plane_cx, &surface.plane_cz);
    surface.min_y = min_y;
    surface.max_y = max_y;
    surface.plane_y = floor_surface ? sector->floor_y : sector->ceil_y;
    surface.atlas_w = lm_surface_texels(surface.max_x - surface.min_x);
    surface.atlas_h = lm_surface_texels(surface.max_z - surface.min_z);
    return lm_surface_list_append(list, &surface);
}

static bool lm_pack_surfaces(lm_surface_list *list, int *atlas_w, int *atlas_h)
{
    for (int size = LM_MIN_ATLAS_SIZE; size <= LM_MAX_ATLAS_SIZE; size *= 2)
    {
        int cursor_x = 0;
        int cursor_y = 0;
        int row_h = 0;
        bool fits = true;

        for (int i = 0; i < list->count; ++i)
        {
            lm_surface *surface = &list->items[i];
            if (surface->atlas_w > size || surface->atlas_h > size)
            {
                fits = false;
                break;
            }
            if (cursor_x + surface->atlas_w > size)
            {
                cursor_x = 0;
                cursor_y += row_h;
                row_h = 0;
            }
            if (cursor_y + surface->atlas_h > size)
            {
                fits = false;
                break;
            }
            surface->atlas_x = cursor_x;
            surface->atlas_y = cursor_y;
            cursor_x += surface->atlas_w;
            if (surface->atlas_h > row_h)
            {
                row_h = surface->atlas_h;
            }
        }

        if (fits)
        {
            *atlas_w = size;
            *atlas_h = size;
            return true;
        }
    }

    return SDL_SetError("Lightmap atlas exceeds %dx%d.", LM_MAX_ATLAS_SIZE, LM_MAX_ATLAS_SIZE);
}

static bool lm_allocate_uvs(macc *accs, int acc_count)
{
    for (int i = 0; i < acc_count; ++i)
    {
        macc *acc = &accs[i];
        if (acc->vc <= 0)
        {
            continue;
        }
        acc->lm_uvs = SDL_calloc((size_t)acc->vc * 2U, sizeof(float));
        if (acc->lm_uvs == NULL)
        {
            return SDL_OutOfMemory();
        }
    }
    return true;
}

static void lm_assign_surface_uvs(macc *acc, const lm_surface *surface, int atlas_w, int atlas_h)
{
    const float u0 = ((float)surface->atlas_x + 0.5f) / (float)atlas_w;
    const float v0 = ((float)surface->atlas_y + 0.5f) / (float)atlas_h;
    const float u1 = ((float)(surface->atlas_x + surface->atlas_w) - 0.5f) / (float)atlas_w;
    const float v1 = ((float)(surface->atlas_y + surface->atlas_h) - 0.5f) / (float)atlas_h;

    if (surface->type == LM_SURFACE_WALL && surface->vertex_count >= 4)
    {
        int base = surface->first_vertex;
        acc->lm_uvs[base * 2 + 0] = u0;
        acc->lm_uvs[base * 2 + 1] = v0;
        acc->lm_uvs[(base + 1) * 2 + 0] = u1;
        acc->lm_uvs[(base + 1) * 2 + 1] = v0;
        acc->lm_uvs[(base + 2) * 2 + 0] = u1;
        acc->lm_uvs[(base + 2) * 2 + 1] = v1;
        acc->lm_uvs[(base + 3) * 2 + 0] = u0;
        acc->lm_uvs[(base + 3) * 2 + 1] = v1;
        return;
    }

    for (int i = 0; i < surface->vertex_count; ++i)
    {
        int vi = surface->first_vertex + i;
        float x = acc->pos[vi * 3 + 0];
        float z = acc->pos[vi * 3 + 2];
        float sx = surface->max_x > surface->min_x ? (x - surface->min_x) / (surface->max_x - surface->min_x) : 0.0f;
        float sz = surface->max_z > surface->min_z ? (z - surface->min_z) / (surface->max_z - surface->min_z) : 0.0f;
        acc->lm_uvs[vi * 2 + 0] = u0 + sx * (u1 - u0);
        acc->lm_uvs[vi * 2 + 1] = v0 + sz * (v1 - v0);
    }
}

static Uint8 lm_channel_clamp(float value)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    if (value >= 255.0f)
    {
        return 255;
    }
    return (Uint8)(value + 0.5f);
}

static bool lm_build_texture(sdl3d_level *out)
{
    sdl3d_image image;
    size_t pixel_count = (size_t)out->lightmap_width * (size_t)out->lightmap_height;

    if (out->lightmap_pixels == NULL || out->lightmap_width <= 0 || out->lightmap_height <= 0)
    {
        return true;
    }

    SDL_zero(image);
    image.width = out->lightmap_width;
    image.height = out->lightmap_height;
    image.pixels = SDL_malloc(pixel_count * 4U);
    if (image.pixels == NULL)
    {
        return SDL_OutOfMemory();
    }

    for (size_t i = 0; i < pixel_count; ++i)
    {
        image.pixels[i * 4 + 0] = out->lightmap_pixels[i * 3 + 0];
        image.pixels[i * 4 + 1] = out->lightmap_pixels[i * 3 + 1];
        image.pixels[i * 4 + 2] = out->lightmap_pixels[i * 3 + 2];
        image.pixels[i * 4 + 3] = 255;
    }

    if (!sdl3d_create_texture_from_image(&image, &out->lightmap_texture))
    {
        SDL_free(image.pixels);
        return false;
    }
    SDL_free(image.pixels);
    if (!sdl3d_set_texture_filter(&out->lightmap_texture, SDL3D_TEXTURE_FILTER_BILINEAR) ||
        !sdl3d_set_texture_wrap(&out->lightmap_texture, SDL3D_TEXTURE_WRAP_CLAMP, SDL3D_TEXTURE_WRAP_CLAMP))
    {
        sdl3d_free_texture(&out->lightmap_texture);
        return false;
    }
    return true;
}

static bool lm_bake_lightmap(const lm_surface_list *list, const sdl3d_level_light *lights, int light_count,
                             sdl3d_level *out)
{
    const int atlas_w = out->lightmap_width;
    const int atlas_h = out->lightmap_height;
    unsigned char *pixels;

    if (atlas_w <= 0 || atlas_h <= 0)
    {
        return true;
    }

    pixels = SDL_malloc((size_t)atlas_w * (size_t)atlas_h * 3U);
    if (pixels == NULL)
    {
        return SDL_OutOfMemory();
    }

    for (int i = 0; i < atlas_w * atlas_h; ++i)
    {
        pixels[i * 3 + 0] = 51;
        pixels[i * 3 + 1] = 51;
        pixels[i * 3 + 2] = 51;
    }

    for (int si = 0; si < list->count; ++si)
    {
        const lm_surface *surface = &list->items[si];
        for (int y = 0; y < surface->atlas_h; ++y)
        {
            for (int x = 0; x < surface->atlas_w; ++x)
            {
                float s = surface->atlas_w > 1 ? (float)x / (float)(surface->atlas_w - 1) : 0.0f;
                float t = surface->atlas_h > 1 ? (float)y / (float)(surface->atlas_h - 1) : 0.0f;
                float wx, wy, wz;
                float lr = 51.0f;
                float lg = 51.0f;
                float lb = 51.0f;
                int atlas_index;

                if (surface->type == LM_SURFACE_WALL)
                {
                    float bottom = surface->bot0 + (surface->bot1 - surface->bot0) * s;
                    float top = surface->top0 + (surface->top1 - surface->top0) * s;
                    wx = surface->x0 + (surface->x1 - surface->x0) * s;
                    wy = bottom + (top - bottom) * t;
                    wz = surface->z0 + (surface->z1 - surface->z0) * s;
                }
                else
                {
                    wx = surface->min_x + (surface->max_x - surface->min_x) * s;
                    wz = surface->min_z + (surface->max_z - surface->min_z) * t;
                    wy =
                        surface->plane_y -
                        (surface->nx * (wx - surface->plane_cx) + surface->nz * (wz - surface->plane_cz)) / surface->ny;
                }

                for (int li = 0; li < light_count; ++li)
                {
                    float lx = lights[li].position[0] - wx;
                    float ly = lights[li].position[1] - wy;
                    float lz = lights[li].position[2] - wz;
                    float dist = SDL_sqrtf(lx * lx + ly * ly + lz * lz);
                    float inv;
                    float ndotl;
                    float r;
                    float atten;
                    float scale;

                    if (dist < 0.0001f || dist > lights[li].range)
                    {
                        continue;
                    }
                    inv = 1.0f / dist;
                    lx *= inv;
                    ly *= inv;
                    lz *= inv;
                    ndotl = surface->nx * lx + surface->ny * ly + surface->nz * lz;
                    if (ndotl <= 0.0f)
                    {
                        continue;
                    }
                    r = dist / lights[li].range;
                    atten = 1.0f - r * r;
                    if (atten < 0.0f)
                    {
                        atten = 0.0f;
                    }
                    atten *= atten;
                    scale = lights[li].intensity * atten * ndotl * 255.0f;
                    lr += lights[li].color[0] * scale;
                    lg += lights[li].color[1] * scale;
                    lb += lights[li].color[2] * scale;
                }

                atlas_index = ((surface->atlas_y + y) * atlas_w + (surface->atlas_x + x)) * 3;
                pixels[atlas_index + 0] = lm_channel_clamp(lr);
                pixels[atlas_index + 1] = lm_channel_clamp(lg);
                pixels[atlas_index + 2] = lm_channel_clamp(lb);
            }
        }
    }

    out->lightmap_pixels = pixels;
    return true;
}

/* Geometry helpers                                                     */
/* ------------------------------------------------------------------ */

static bool add_wall(macc *a, float x0, float z0, float x1, float z1, float bot0, float top0, float bot1, float top1,
                     const float *rgba, float tex_scale)
{
    if (top0 - bot0 < 0.001f && top1 - bot1 < 0.001f)
        return true;
    float dx = x1 - x0, dz = z1 - z0;
    float len = SDL_sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f)
        return true;
    /* Sector polygons are CCW in XZ, so edge interior lies on the left side.
     * Keep wall winding interior-facing and align the stored normal with it. */
    float nx = -dz / len, nz = dx / len;
    float s = tex_scale > 0 ? tex_scale : 4.0f;
    float u0 = 0.0f, u1 = len / s;
    int v_0 = macc_vert(a, x0, bot0, z0, nx, 0, nz, rgba, u0, bot0 / s);
    int v_1 = macc_vert(a, x1, bot1, z1, nx, 0, nz, rgba, u1, bot1 / s);
    int v_2 = macc_vert(a, x1, top1, z1, nx, 0, nz, rgba, u1, top1 / s);
    int v_3 = macc_vert(a, x0, top0, z0, nx, 0, nz, rgba, u0, top0 / s);
    if (v_0 < 0)
        return false;
    return macc_tri(a, v_0, v_1, v_2) && macc_tri(a, v_0, v_2, v_3);
}

static bool add_floor_ceil(macc *a, const sdl3d_sector *s, bool floor_surface, const float *rgba, float tex_scale)
{
    int n = s->num_points;
    sdl3d_vec3 normal = floor_surface ? sdl3d_sector_floor_normal(s) : sdl3d_sector_ceil_normal(s);
    if (n < 3)
        return true;
    int base = a->vc;
    float sc = tex_scale > 0 ? tex_scale : 4.0f;
    for (int i = 0; i < n; i++)
    {
        float x = s->points[i][0];
        float z = s->points[i][1];
        float y = floor_surface ? sdl3d_sector_floor_at(s, x, z) : sdl3d_sector_ceil_at(s, x, z);
        if (macc_vert(a, x, y, z, normal.x, normal.y, normal.z, rgba, x / sc, z / sc) < 0)
            return false;
    }
    for (int i = 1; i < n - 1; i++)
    {
        if (floor_surface)
        {
            if (!macc_tri(a, base, base + i + 1, base + i))
                return false;
        }
        else
        {
            if (!macc_tri(a, base, base + i, base + i + 1))
                return false;
        }
    }
    return true;
}

static bool add_wall_and_lightmap(macc *wall_acc, lm_surface_list *surf_list, int wall_acc_index, float x0, float z0,
                                  float x1, float z1, float bot0, float top0, float bot1, float top1,
                                  const sdl3d_level_material *material)
{
    int vb = wall_acc->vc;
    return add_wall(wall_acc, x0, z0, x1, z1, bot0, top0, bot1, top1, material->albedo, material->tex_scale) &&
           lm_record_wall_surface(surf_list, wall_acc_index, wall_acc, vb, x0, z0, x1, z1, bot0, top0, bot1, top1);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

static void sector_apply_runtime_geometry(sdl3d_sector *sector, const sdl3d_sector_geometry *geometry)
{
    sector->floor_y = geometry->floor_y;
    sector->ceil_y = geometry->ceil_y;
    SDL_memcpy(sector->floor_normal, geometry->floor_normal, sizeof(sector->floor_normal));
    SDL_memcpy(sector->ceil_normal, geometry->ceil_normal, sizeof(sector->ceil_normal));
}

static void free_mesh_geometry(sdl3d_mesh *mesh)
{
    if (mesh == NULL)
    {
        return;
    }

    SDL_free(mesh->name);
    SDL_free(mesh->positions);
    SDL_free(mesh->normals);
    SDL_free(mesh->uvs);
    SDL_free(mesh->lightmap_uvs);
    SDL_free(mesh->colors);
    SDL_free(mesh->indices);
    SDL_free(mesh->joint_indices);
    SDL_free(mesh->joint_weights);
    SDL_zero(*mesh);
}

static edge_info *build_level_edges(const sdl3d_sector *sectors, int sector_count, int *out_edge_count)
{
    int total_edges = 0;
    edge_info *edges;
    int ei = 0;

    for (int i = 0; i < sector_count; i++)
    {
        total_edges += sectors[i].num_points;
    }

    edges = SDL_calloc((size_t)total_edges, sizeof(*edges));
    if (edges == NULL)
    {
        SDL_OutOfMemory();
        return NULL;
    }

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        for (int e = 0; e < sec->num_points; e++)
        {
            int next = (e + 1) % sec->num_points;
            edges[ei].ax = sec->points[e][0];
            edges[ei].az = sec->points[e][1];
            edges[ei].bx = sec->points[next][0];
            edges[ei].bz = sec->points[next][1];
            edges[ei].sector = s;
            ei++;
        }
    }

    *out_edge_count = total_edges;
    return edges;
}

static bool append_portal(sdl3d_level_portal **portals, int *portal_count, int *portal_cap,
                          const sdl3d_sector *sectors, int sector_a, int sector_b, float x0, float z0, float x1,
                          float z1)
{
    const sdl3d_sector *a = &sectors[sector_a];
    const sdl3d_sector *b = &sectors[sector_b];
    float a_floor0 = sdl3d_sector_floor_at(a, x0, z0);
    float a_floor1 = sdl3d_sector_floor_at(a, x1, z1);
    float b_floor0 = sdl3d_sector_floor_at(b, x0, z0);
    float b_floor1 = sdl3d_sector_floor_at(b, x1, z1);
    float a_ceil0 = sdl3d_sector_ceil_at(a, x0, z0);
    float a_ceil1 = sdl3d_sector_ceil_at(a, x1, z1);
    float b_ceil0 = sdl3d_sector_ceil_at(b, x0, z0);
    float b_ceil1 = sdl3d_sector_ceil_at(b, x1, z1);
    float p_floor0 = a_floor0 > b_floor0 ? a_floor0 : b_floor0;
    float p_floor1 = a_floor1 > b_floor1 ? a_floor1 : b_floor1;
    float p_ceil0 = a_ceil0 < b_ceil0 ? a_ceil0 : b_ceil0;
    float p_ceil1 = a_ceil1 < b_ceil1 ? a_ceil1 : b_ceil1;
    sdl3d_level_portal *p;

    if (*portal_count >= *portal_cap)
    {
        int new_cap = *portal_cap > 0 ? *portal_cap * 2 : 32;
        sdl3d_level_portal *new_portals = SDL_realloc(*portals, (size_t)new_cap * sizeof(*new_portals));
        if (new_portals == NULL)
        {
            return SDL_OutOfMemory();
        }
        *portals = new_portals;
        *portal_cap = new_cap;
    }

    p = &(*portals)[(*portal_count)++];
    p->sector_a = sector_a;
    p->sector_b = sector_b;
    p->min_x = x0 < x1 ? x0 : x1;
    p->max_x = x0 > x1 ? x0 : x1;
    p->min_z = z0 < z1 ? z0 : z1;
    p->max_z = z0 > z1 ? z0 : z1;
    p->floor_y = p_floor0 < p_floor1 ? p_floor0 : p_floor1;
    p->ceil_y = p_ceil0 > p_ceil1 ? p_ceil0 : p_ceil1;
    return true;
}

static bool rebuild_level_portals(const sdl3d_sector *sectors, const edge_info *edges, int edge_count,
                                  sdl3d_level_portal **out_portals, int *out_portal_count)
{
    sdl3d_level_portal *portals = NULL;
    int portal_count = 0;
    int portal_cap = 0;

    for (int i = 0; i < edge_count; ++i)
    {
        float ax = edges[i].ax;
        float az = edges[i].az;
        float bx = edges[i].bx;
        float bz = edges[i].bz;

        for (int j = i + 1; j < edge_count; ++j)
        {
            float t0;
            float t1;
            float px0;
            float pz0;
            float px1;
            float pz1;

            if (edges[j].sector == edges[i].sector)
            {
                continue;
            }
            if (edges[i].sector > edges[j].sector)
            {
                continue;
            }
            if (!edges_collinear(ax, az, bx, bz, edges[j].ax, edges[j].az, edges[j].bx, edges[j].bz))
            {
                continue;
            }

            t0 = project_onto_edge(edges[j].ax, edges[j].az, ax, az, bx, bz);
            t1 = project_onto_edge(edges[j].bx, edges[j].bz, ax, az, bx, bz);
            if (t0 > t1)
            {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 < 0.0f)
                t0 = 0.0f;
            if (t1 > 1.0f)
                t1 = 1.0f;
            if (t1 - t0 <= 0.001f)
            {
                continue;
            }

            px0 = ax + (bx - ax) * t0;
            pz0 = az + (bz - az) * t0;
            px1 = ax + (bx - ax) * t1;
            pz1 = az + (bz - az) * t1;
            if (!append_portal(&portals, &portal_count, &portal_cap, sectors, edges[i].sector, edges[j].sector, px0,
                               pz0, px1, pz1))
            {
                SDL_free(portals);
                return false;
            }
        }
    }

    *out_portals = portals;
    *out_portal_count = portal_count;
    return true;
}

static bool mark_dirty_sector_neighbors(const edge_info *edges, int edge_count, int sector_index, bool *dirty)
{
    dirty[sector_index] = true;

    for (int i = 0; i < edge_count; ++i)
    {
        float ax;
        float az;
        float bx;
        float bz;

        if (edges[i].sector != sector_index)
        {
            continue;
        }

        ax = edges[i].ax;
        az = edges[i].az;
        bx = edges[i].bx;
        bz = edges[i].bz;
        for (int j = 0; j < edge_count; ++j)
        {
            float t0;
            float t1;

            if (edges[j].sector == sector_index)
            {
                continue;
            }
            if (!edges_collinear(ax, az, bx, bz, edges[j].ax, edges[j].az, edges[j].bx, edges[j].bz))
            {
                continue;
            }

            t0 = project_onto_edge(edges[j].ax, edges[j].az, ax, az, bx, bz);
            t1 = project_onto_edge(edges[j].bx, edges[j].bz, ax, az, bx, bz);
            if (t0 > t1)
            {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 < 0.0f)
                t0 = 0.0f;
            if (t1 > 1.0f)
                t1 = 1.0f;
            if (t1 - t0 > 0.001f)
            {
                dirty[edges[j].sector] = true;
            }
        }
    }

    return true;
}

static bool add_wall_runtime(macc *wall_acc, float x0, float z0, float x1, float z1, float bot0, float top0,
                             float bot1, float top1, const sdl3d_level_material *material)
{
    return add_wall(wall_acc, x0, z0, x1, z1, bot0, top0, bot1, top1, material->albedo, material->tex_scale);
}

static bool build_dirty_sector_accs(const sdl3d_sector *sectors, int sector_count, const edge_info *edges,
                                    int edge_count, const bool *dirty, const sdl3d_level_material *materials,
                                    int material_count, macc *accs)
{
    (void)sector_count;

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec;
        int fi;
        int ci;
        int wi;
        int wall_acc_index;
        macc *wall_acc;

        if (!dirty[s])
        {
            continue;
        }

        sec = &sectors[s];
        fi = (sec->floor_material >= 0 && sec->floor_material < material_count) ? sec->floor_material : -1;
        ci = (sec->ceil_material >= 0 && sec->ceil_material < material_count) ? sec->ceil_material : -1;
        wi = sec->wall_material < material_count ? sec->wall_material : 0;
        wall_acc_index = s * material_count + wi;
        wall_acc = &accs[wall_acc_index];

        if (fi >= 0)
        {
            macc *floor_acc = &accs[s * material_count + fi];
            if (!add_floor_ceil(floor_acc, sec, true, materials[fi].albedo, materials[fi].tex_scale))
            {
                return false;
            }
        }
        if (ci >= 0)
        {
            macc *ceil_acc = &accs[s * material_count + ci];
            if (!add_floor_ceil(ceil_acc, sec, false, materials[ci].albedo, materials[ci].tex_scale))
            {
                return false;
            }
        }

        for (int e = 0; e < edge_count; e++)
        {
            float ax;
            float az;
            float bx;
            float bz;
            overlap_t overlaps[32];
            int novl = 0;

            if (edges[e].sector != s)
            {
                continue;
            }

            ax = edges[e].ax;
            az = edges[e].az;
            bx = edges[e].bx;
            bz = edges[e].bz;

            for (int j = 0; j < edge_count && novl < 32; j++)
            {
                float t0;
                float t1;

                if (edges[j].sector == s)
                {
                    continue;
                }
                if (!edges_collinear(ax, az, bx, bz, edges[j].ax, edges[j].az, edges[j].bx, edges[j].bz))
                {
                    continue;
                }
                t0 = project_onto_edge(edges[j].ax, edges[j].az, ax, az, bx, bz);
                t1 = project_onto_edge(edges[j].bx, edges[j].bz, ax, az, bx, bz);
                if (t0 > t1)
                {
                    float tmp = t0;
                    t0 = t1;
                    t1 = tmp;
                }
                if (t0 < 0)
                    t0 = 0;
                if (t1 > 1)
                    t1 = 1;
                if (t1 - t0 > 0.001f)
                {
                    overlaps[novl].t0 = t0;
                    overlaps[novl].t1 = t1;
                    overlaps[novl].other_sector = edges[j].sector;
                    novl++;
                }
            }

            if (novl == 0)
            {
                if (!add_wall_runtime(wall_acc, ax, az, bx, bz, sdl3d_sector_floor_at(sec, ax, az),
                                      sdl3d_sector_ceil_at(sec, ax, az), sdl3d_sector_floor_at(sec, bx, bz),
                                      sdl3d_sector_ceil_at(sec, bx, bz), &materials[wi]))
                {
                    return false;
                }
            }
            else
            {
                for (int i = 0; i < novl - 1; i++)
                    for (int j = i + 1; j < novl; j++)
                        if (overlaps[j].t0 < overlaps[i].t0)
                        {
                            overlap_t tmp = overlaps[i];
                            overlaps[i] = overlaps[j];
                            overlaps[j] = tmp;
                        }

                float cursor = 0.0f;
                for (int oi = 0; oi < novl; oi++)
                {
                    float ox0;
                    float oz0;
                    float ox1;
                    float oz1;
                    const sdl3d_sector *other;
                    float sec_floor0;
                    float sec_floor1;
                    float other_floor0;
                    float other_floor1;
                    float sec_ceil0;
                    float sec_ceil1;
                    float other_ceil0;
                    float other_ceil1;

                    if (overlaps[oi].t0 > cursor + 0.001f)
                    {
                        float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                        float wx1 = ax + (bx - ax) * overlaps[oi].t0, wz1 = az + (bz - az) * overlaps[oi].t0;
                        if (!add_wall_runtime(wall_acc, wx0, wz0, wx1, wz1, sdl3d_sector_floor_at(sec, wx0, wz0),
                                              sdl3d_sector_ceil_at(sec, wx0, wz0),
                                              sdl3d_sector_floor_at(sec, wx1, wz1),
                                              sdl3d_sector_ceil_at(sec, wx1, wz1), &materials[wi]))
                        {
                            return false;
                        }
                    }

                    ox0 = ax + (bx - ax) * overlaps[oi].t0;
                    oz0 = az + (bz - az) * overlaps[oi].t0;
                    ox1 = ax + (bx - ax) * overlaps[oi].t1;
                    oz1 = az + (bz - az) * overlaps[oi].t1;
                    other = &sectors[overlaps[oi].other_sector];
                    sec_floor0 = sdl3d_sector_floor_at(sec, ox0, oz0);
                    sec_floor1 = sdl3d_sector_floor_at(sec, ox1, oz1);
                    other_floor0 = sdl3d_sector_floor_at(other, ox0, oz0);
                    other_floor1 = sdl3d_sector_floor_at(other, ox1, oz1);
                    sec_ceil0 = sdl3d_sector_ceil_at(sec, ox0, oz0);
                    sec_ceil1 = sdl3d_sector_ceil_at(sec, ox1, oz1);
                    other_ceil0 = sdl3d_sector_ceil_at(other, ox0, oz0);
                    other_ceil1 = sdl3d_sector_ceil_at(other, ox1, oz1);
                    if (sec_floor0 < other_floor0 - 0.001f || sec_floor1 < other_floor1 - 0.001f)
                    {
                        if (!add_wall_runtime(wall_acc, ox0, oz0, ox1, oz1, sec_floor0, other_floor0, sec_floor1,
                                              other_floor1, &materials[wi]))
                        {
                            return false;
                        }
                    }
                    if (sec_ceil0 > other_ceil0 + 0.001f || sec_ceil1 > other_ceil1 + 0.001f)
                    {
                        if (!add_wall_runtime(wall_acc, ox0, oz0, ox1, oz1, other_ceil0, sec_ceil0, other_ceil1,
                                              sec_ceil1, &materials[wi]))
                        {
                            return false;
                        }
                    }
                    cursor = overlaps[oi].t1;
                }
                if (cursor < 1.0f - 0.001f)
                {
                    float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                    if (!add_wall_runtime(wall_acc, wx0, wz0, bx, bz, sdl3d_sector_floor_at(sec, wx0, wz0),
                                          sdl3d_sector_ceil_at(sec, wx0, wz0), sdl3d_sector_floor_at(sec, bx, bz),
                                          sdl3d_sector_ceil_at(sec, bx, bz), &materials[wi]))
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

static void bake_vertex_lighting_for_accs(macc *accs, int acc_count, const sdl3d_level_light *lights, int light_count)
{
    if (lights == NULL || light_count <= 0)
    {
        return;
    }

    for (int ai = 0; ai < acc_count; ai++)
    {
        macc *a = &accs[ai];
        for (int v = 0; v < a->vc; v++)
        {
            float px = a->pos[v * 3], py = a->pos[v * 3 + 1], pz = a->pos[v * 3 + 2];
            float nx = a->nrm[v * 3], ny = a->nrm[v * 3 + 1], nz = a->nrm[v * 3 + 2];
            float br = a->col[v * 4], bg = a->col[v * 4 + 1], bb = a->col[v * 4 + 2];
            float lr = 0.2f * br, lg = 0.2f * bg, lb = 0.2f * bb;
            for (int li = 0; li < light_count; li++)
            {
                float lx = lights[li].position[0] - px, ly = lights[li].position[1] - py,
                      lz = lights[li].position[2] - pz;
                float dist = SDL_sqrtf(lx * lx + ly * ly + lz * lz);
                float inv;
                float ndotl;
                float r;
                float atten;
                float scale;

                if (dist < 0.0001f || dist > lights[li].range)
                    continue;
                inv = 1.0f / dist;
                lx *= inv;
                ly *= inv;
                lz *= inv;
                ndotl = nx * lx + ny * ly + nz * lz;
                if (ndotl <= 0.0f)
                    continue;
                r = dist / lights[li].range;
                atten = (1.0f - r * r);
                if (atten < 0)
                    atten = 0;
                atten *= atten;
                scale = lights[li].intensity * atten * ndotl;
                lr += br * lights[li].color[0] * scale;
                lg += bg * lights[li].color[1] * scale;
                lb += bb * lights[li].color[2] * scale;
            }
            a->col[v * 4] = lr > 1.0f ? 1.0f : lr;
            a->col[v * 4 + 1] = lg > 1.0f ? 1.0f : lg;
            a->col[v * 4 + 2] = lb > 1.0f ? 1.0f : lb;
        }
    }
}

static void move_acc_to_mesh(sdl3d_mesh *mesh, macc *acc, int material_index, bool baked_light)
{
    SDL_zero(*mesh);
    mesh->vertex_count = acc->vc;
    mesh->index_count = acc->ic;
    mesh->positions = acc->pos;
    mesh->normals = acc->nrm;
    mesh->colors = acc->col;
    mesh->colors_are_baked_light = baked_light;
    mesh->uvs = acc->uvs;
    mesh->lightmap_uvs = acc->lm_uvs;
    mesh->indices = acc->idx;
    mesh->material_index = material_index;
    mesh->has_local_bounds = acc->has_bounds;
    mesh->local_bounds = acc->bounds;
    mesh->dynamic_geometry = true;
    acc->pos = NULL;
    acc->nrm = NULL;
    acc->col = NULL;
    acc->uvs = NULL;
    acc->lm_uvs = NULL;
    acc->idx = NULL;
    acc->vc = 0;
    acc->ic = 0;
}

bool sdl3d_build_level(const sdl3d_sector *sectors, int sector_count, const sdl3d_level_material *materials,
                       int material_count, const sdl3d_level_light *lights, int light_count, sdl3d_level *out)
{
    lm_surface_list surf_list;

    if (!sectors || sector_count <= 0 || !out)
        return SDL_InvalidParamError("sectors");
    if (!materials || material_count <= 0)
        return SDL_InvalidParamError("materials");

    SDL_zerop(out);
    SDL_zero(surf_list);

    /* Build edge list. */
    int total_edges = 0;
    for (int i = 0; i < sector_count; i++)
        total_edges += sectors[i].num_points;

    edge_info *edges = SDL_calloc((size_t)total_edges, sizeof(edge_info));
    if (!edges)
        return SDL_OutOfMemory();

    int ei = 0;
    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        for (int e = 0; e < sec->num_points; e++)
        {
            int next = (e + 1) % sec->num_points;
            edges[ei].ax = sec->points[e][0];
            edges[ei].az = sec->points[e][1];
            edges[ei].bx = sec->points[next][0];
            edges[ei].bz = sec->points[next][1];
            edges[ei].sector = s;
            ei++;
        }
    }

    /* Per-sector/per-material mesh accumulators so hidden rooms can be culled
     * independently without changing the current baked-light look. */
    const int acc_count = sector_count * material_count;
    macc *accs = SDL_calloc((size_t)acc_count, sizeof(macc));
    if (!accs)
    {
        SDL_free(edges);
        return SDL_OutOfMemory();
    }

    /* Portal accumulator — collect adjacencies during edge overlap detection. */
    int portal_cap = 32, portal_count = 0;
    sdl3d_level_portal *portals = SDL_malloc((size_t)portal_cap * sizeof(sdl3d_level_portal));
    if (!portals)
    {
        for (int ai = 0; ai < acc_count; ai++)
            macc_free(&accs[ai]);
        SDL_free(accs);
        SDL_free(edges);
        return SDL_OutOfMemory();
    }

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        int fi = (sec->floor_material >= 0 && sec->floor_material < material_count) ? sec->floor_material : -1;
        int ci = (sec->ceil_material >= 0 && sec->ceil_material < material_count) ? sec->ceil_material : -1;
        int wi = sec->wall_material < material_count ? sec->wall_material : 0;
        int wall_acc_index = s * material_count + wi;
        macc *wall_acc = &accs[wall_acc_index];

        if (fi >= 0)
        {
            int floor_acc_index = s * material_count + fi;
            macc *floor_acc = &accs[floor_acc_index];
            int vb = floor_acc->vc;
            if (!add_floor_ceil(floor_acc, sec, true, materials[fi].albedo, materials[fi].tex_scale) ||
                !lm_record_floor_ceil_surface(&surf_list, floor_acc_index, floor_acc, vb, sec, true))
            {
                goto fail;
            }
        }
        if (ci >= 0)
        {
            int ceil_acc_index = s * material_count + ci;
            macc *ceil_acc = &accs[ceil_acc_index];
            int vb = ceil_acc->vc;
            if (!add_floor_ceil(ceil_acc, sec, false, materials[ci].albedo, materials[ci].tex_scale) ||
                !lm_record_floor_ceil_surface(&surf_list, ceil_acc_index, ceil_acc, vb, sec, false))
            {
                goto fail;
            }
        }

        for (int e = 0; e < total_edges; e++)
        {
            if (edges[e].sector != s)
                continue;

            float ax = edges[e].ax, az = edges[e].az;
            float bx = edges[e].bx, bz = edges[e].bz;

            overlap_t overlaps[32];
            int novl = 0;

            for (int j = 0; j < total_edges && novl < 32; j++)
            {
                if (edges[j].sector == s)
                    continue;
                if (!edges_collinear(ax, az, bx, bz, edges[j].ax, edges[j].az, edges[j].bx, edges[j].bz))
                    continue;
                float t0 = project_onto_edge(edges[j].ax, edges[j].az, ax, az, bx, bz);
                float t1 = project_onto_edge(edges[j].bx, edges[j].bz, ax, az, bx, bz);
                if (t0 > t1)
                {
                    float tmp = t0;
                    t0 = t1;
                    t1 = tmp;
                }
                if (t0 < 0)
                    t0 = 0;
                if (t1 > 1)
                    t1 = 1;
                if (t1 - t0 > 0.001f)
                {
                    overlaps[novl].t0 = t0;
                    overlaps[novl].t1 = t1;
                    overlaps[novl].other_sector = edges[j].sector;
                    novl++;

                    /* Record portal (only when s < neighbor to avoid duplicates). */
                    if (s < edges[j].sector)
                    {
                        if (portal_count >= portal_cap)
                        {
                            portal_cap *= 2;
                            sdl3d_level_portal *tmp =
                                SDL_realloc(portals, (size_t)portal_cap * sizeof(sdl3d_level_portal));
                            if (tmp)
                                portals = tmp;
                        }
                        if (portal_count < portal_cap)
                        {
                            float px0 = ax + (bx - ax) * t0, pz0 = az + (bz - az) * t0;
                            float px1 = ax + (bx - ax) * t1, pz1 = az + (bz - az) * t1;
                            const sdl3d_sector *other = &sectors[edges[j].sector];
                            float sec_floor0 = sdl3d_sector_floor_at(sec, px0, pz0);
                            float sec_floor1 = sdl3d_sector_floor_at(sec, px1, pz1);
                            float other_floor0 = sdl3d_sector_floor_at(other, px0, pz0);
                            float other_floor1 = sdl3d_sector_floor_at(other, px1, pz1);
                            float sec_ceil0 = sdl3d_sector_ceil_at(sec, px0, pz0);
                            float sec_ceil1 = sdl3d_sector_ceil_at(sec, px1, pz1);
                            float other_ceil0 = sdl3d_sector_ceil_at(other, px0, pz0);
                            float other_ceil1 = sdl3d_sector_ceil_at(other, px1, pz1);
                            float p_floor0 = sec_floor0 > other_floor0 ? sec_floor0 : other_floor0;
                            float p_floor1 = sec_floor1 > other_floor1 ? sec_floor1 : other_floor1;
                            float p_ceil0 = sec_ceil0 < other_ceil0 ? sec_ceil0 : other_ceil0;
                            float p_ceil1 = sec_ceil1 < other_ceil1 ? sec_ceil1 : other_ceil1;
                            sdl3d_level_portal *p = &portals[portal_count++];
                            p->sector_a = s;
                            p->sector_b = edges[j].sector;
                            p->min_x = px0 < px1 ? px0 : px1;
                            p->max_x = px0 > px1 ? px0 : px1;
                            p->min_z = pz0 < pz1 ? pz0 : pz1;
                            p->max_z = pz0 > pz1 ? pz0 : pz1;
                            p->floor_y = p_floor0 < p_floor1 ? p_floor0 : p_floor1;
                            p->ceil_y = p_ceil0 > p_ceil1 ? p_ceil0 : p_ceil1;
                        }
                    }
                }
            }

            if (novl == 0)
            {
                if (!add_wall_and_lightmap(wall_acc, &surf_list, wall_acc_index, ax, az, bx, bz,
                                           sdl3d_sector_floor_at(sec, ax, az), sdl3d_sector_ceil_at(sec, ax, az),
                                           sdl3d_sector_floor_at(sec, bx, bz), sdl3d_sector_ceil_at(sec, bx, bz),
                                           &materials[wi]))
                {
                    goto fail;
                }
            }
            else
            {
                for (int i = 0; i < novl - 1; i++)
                    for (int j = i + 1; j < novl; j++)
                        if (overlaps[j].t0 < overlaps[i].t0)
                        {
                            overlap_t tmp = overlaps[i];
                            overlaps[i] = overlaps[j];
                            overlaps[j] = tmp;
                        }

                float cursor = 0.0f;
                for (int oi = 0; oi < novl; oi++)
                {
                    if (overlaps[oi].t0 > cursor + 0.001f)
                    {
                        float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                        float wx1 = ax + (bx - ax) * overlaps[oi].t0, wz1 = az + (bz - az) * overlaps[oi].t0;
                        if (!add_wall_and_lightmap(wall_acc, &surf_list, wall_acc_index, wx0, wz0, wx1, wz1,
                                                   sdl3d_sector_floor_at(sec, wx0, wz0),
                                                   sdl3d_sector_ceil_at(sec, wx0, wz0),
                                                   sdl3d_sector_floor_at(sec, wx1, wz1),
                                                   sdl3d_sector_ceil_at(sec, wx1, wz1), &materials[wi]))
                        {
                            goto fail;
                        }
                    }
                    float ox0 = ax + (bx - ax) * overlaps[oi].t0, oz0 = az + (bz - az) * overlaps[oi].t0;
                    float ox1 = ax + (bx - ax) * overlaps[oi].t1, oz1 = az + (bz - az) * overlaps[oi].t1;
                    const sdl3d_sector *other = &sectors[overlaps[oi].other_sector];
                    float sec_floor0 = sdl3d_sector_floor_at(sec, ox0, oz0);
                    float sec_floor1 = sdl3d_sector_floor_at(sec, ox1, oz1);
                    float other_floor0 = sdl3d_sector_floor_at(other, ox0, oz0);
                    float other_floor1 = sdl3d_sector_floor_at(other, ox1, oz1);
                    float sec_ceil0 = sdl3d_sector_ceil_at(sec, ox0, oz0);
                    float sec_ceil1 = sdl3d_sector_ceil_at(sec, ox1, oz1);
                    float other_ceil0 = sdl3d_sector_ceil_at(other, ox0, oz0);
                    float other_ceil1 = sdl3d_sector_ceil_at(other, ox1, oz1);
                    if (sec_floor0 < other_floor0 - 0.001f || sec_floor1 < other_floor1 - 0.001f)
                    {
                        if (!add_wall_and_lightmap(wall_acc, &surf_list, wall_acc_index, ox0, oz0, ox1, oz1, sec_floor0,
                                                   other_floor0, sec_floor1, other_floor1, &materials[wi]))
                        {
                            goto fail;
                        }
                    }
                    if (sec_ceil0 > other_ceil0 + 0.001f || sec_ceil1 > other_ceil1 + 0.001f)
                    {
                        if (!add_wall_and_lightmap(wall_acc, &surf_list, wall_acc_index, ox0, oz0, ox1, oz1,
                                                   other_ceil0, sec_ceil0, other_ceil1, sec_ceil1, &materials[wi]))
                        {
                            goto fail;
                        }
                    }
                    cursor = overlaps[oi].t1;
                }
                if (cursor < 1.0f - 0.001f)
                {
                    float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                    if (!add_wall_and_lightmap(wall_acc, &surf_list, wall_acc_index, wx0, wz0, bx, bz,
                                               sdl3d_sector_floor_at(sec, wx0, wz0),
                                               sdl3d_sector_ceil_at(sec, wx0, wz0), sdl3d_sector_floor_at(sec, bx, bz),
                                               sdl3d_sector_ceil_at(sec, bx, bz), &materials[wi]))
                    {
                        goto fail;
                    }
                }
            }
        }
    }

    SDL_free(edges);
    edges = NULL;

    /* ---- Bake vertex lighting per material ---- */
    if (lights && light_count > 0)
    {
        for (int ai = 0; ai < acc_count; ai++)
        {
            macc *a = &accs[ai];
            for (int v = 0; v < a->vc; v++)
            {
                float px = a->pos[v * 3], py = a->pos[v * 3 + 1], pz = a->pos[v * 3 + 2];
                float nx = a->nrm[v * 3], ny = a->nrm[v * 3 + 1], nz = a->nrm[v * 3 + 2];
                float br = a->col[v * 4], bg = a->col[v * 4 + 1], bb = a->col[v * 4 + 2];
                float lr = 0.2f * br, lg = 0.2f * bg, lb = 0.2f * bb;
                for (int li = 0; li < light_count; li++)
                {
                    float lx = lights[li].position[0] - px, ly = lights[li].position[1] - py,
                          lz = lights[li].position[2] - pz;
                    float dist = SDL_sqrtf(lx * lx + ly * ly + lz * lz);
                    if (dist < 0.0001f || dist > lights[li].range)
                        continue;
                    float inv = 1.0f / dist;
                    lx *= inv;
                    ly *= inv;
                    lz *= inv;
                    float ndotl = nx * lx + ny * ly + nz * lz;
                    if (ndotl <= 0.0f)
                        continue;
                    float r = dist / lights[li].range;
                    float atten = (1.0f - r * r);
                    if (atten < 0)
                        atten = 0;
                    atten *= atten;
                    float scale = lights[li].intensity * atten * ndotl;
                    lr += br * lights[li].color[0] * scale;
                    lg += bg * lights[li].color[1] * scale;
                    lb += bb * lights[li].color[2] * scale;
                }
                a->col[v * 4] = lr > 1.0f ? 1.0f : lr;
                a->col[v * 4 + 1] = lg > 1.0f ? 1.0f : lg;
                a->col[v * 4 + 2] = lb > 1.0f ? 1.0f : lb;
            }
        }

        if (surf_list.count > 0)
        {
            int atlas_w = 0;
            int atlas_h = 0;

            if (!lm_allocate_uvs(accs, acc_count) || !lm_pack_surfaces(&surf_list, &atlas_w, &atlas_h))
            {
                goto fail;
            }

            out->lightmap_width = atlas_w;
            out->lightmap_height = atlas_h;

            for (int si = 0; si < surf_list.count; ++si)
            {
                const lm_surface *surface = &surf_list.items[si];
                lm_assign_surface_uvs(&accs[surface->acc_index], surface, atlas_w, atlas_h);
            }

            if (!lm_bake_lightmap(&surf_list, lights, light_count, out) || !lm_build_texture(out))
            {
                goto fail;
            }
        }
    }

    /* ---- Package: one mesh per sector/material chunk ---- */
    int num_meshes = 0;
    for (int ai = 0; ai < acc_count; ai++)
        if (accs[ai].ic > 0)
            num_meshes++;

    sdl3d_mesh *meshes = SDL_calloc((size_t)num_meshes, sizeof(sdl3d_mesh));
    sdl3d_material *out_mats = SDL_calloc((size_t)material_count, sizeof(sdl3d_material));
    int *mesh_sector_ids = SDL_malloc((size_t)num_meshes * sizeof(int));
    if (!meshes || !out_mats || !mesh_sector_ids)
    {
        SDL_free(meshes);
        SDL_free(out_mats);
        SDL_free(mesh_sector_ids);
        SDL_OutOfMemory();
        goto fail;
    }

    for (int m = 0; m < material_count; m++)
    {
        out_mats[m].albedo[0] = out_mats[m].albedo[1] = out_mats[m].albedo[2] = out_mats[m].albedo[3] = 1.0f;
        out_mats[m].roughness = materials[m].roughness;
        out_mats[m].metallic = materials[m].metallic;
        if (materials[m].texture)
            out_mats[m].albedo_map = SDL_strdup(materials[m].texture);
    }

    int mi = 0;
    int total_verts = 0, total_tris = 0;
    for (int s = 0; s < sector_count; s++)
    {
        for (int m = 0; m < material_count; m++)
        {
            macc *a = &accs[s * material_count + m];
            if (a->ic == 0)
            {
                macc_free(a);
                continue;
            }
            meshes[mi].vertex_count = a->vc;
            meshes[mi].index_count = a->ic;
            meshes[mi].positions = a->pos;
            meshes[mi].normals = a->nrm;
            meshes[mi].colors = a->col;
            meshes[mi].colors_are_baked_light = (lights != NULL && light_count > 0);
            meshes[mi].uvs = a->uvs;
            meshes[mi].lightmap_uvs = a->lm_uvs;
            meshes[mi].indices = a->idx;
            meshes[mi].material_index = m;
            meshes[mi].has_local_bounds = a->has_bounds;
            meshes[mi].local_bounds = a->bounds;

            total_verts += a->vc;
            total_tris += a->ic / 3;
            mesh_sector_ids[mi] = s;
            mi++;
        }
    }
    SDL_free(accs);
    SDL_free(surf_list.items);

    /* source_path for texture resolver — use first texture's directory. */
    for (int m = 0; m < material_count; m++)
    {
        if (materials[m].texture)
        {
            out->model.source_path = SDL_strdup(materials[m].texture);
            break;
        }
    }

    out->model.meshes = meshes;
    out->model.mesh_count = num_meshes;
    out->model.materials = out_mats;
    out->model.material_count = material_count;
    out->sector_count = sector_count;
    out->mesh_sector_ids = mesh_sector_ids;
    out->material_count = material_count;
    out->portal_count = portal_count;
    out->portals = portals;

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SDL3D level: %d verts, %d tris, %d meshes, %d portals from %d sectors",
                 total_verts, total_tris, num_meshes, portal_count, sector_count);
    return true;

fail:
    SDL_free(edges);
    for (int ai = 0; ai < acc_count; ++ai)
    {
        macc_free(&accs[ai]);
    }
    SDL_free(accs);
    SDL_free(surf_list.items);
    SDL_free(portals);
    SDL_free(out->lightmap_pixels);
    out->lightmap_pixels = NULL;
    out->lightmap_width = 0;
    out->lightmap_height = 0;
    sdl3d_free_texture(&out->lightmap_texture);
    return false;
}

bool sdl3d_level_set_sector_geometry(sdl3d_level *level, sdl3d_sector *sectors, int sector_count, int sector_index,
                                     const sdl3d_sector_geometry *geometry, const sdl3d_level_material *materials,
                                     int material_count, const sdl3d_level_light *lights, int light_count)
{
    sdl3d_sector *candidate_sectors;
    edge_info *edges;
    int edge_count = 0;
    bool *dirty;
    macc *accs;
    sdl3d_level_portal *new_portals = NULL;
    int new_portal_count = 0;
    sdl3d_mesh *new_meshes = NULL;
    int *new_mesh_sector_ids = NULL;
    int new_mesh_count = 0;
    int acc_count;
    int old_mesh_count;
    bool baked_light;

    if (level == NULL)
    {
        return SDL_InvalidParamError("level");
    }
    if (sectors == NULL || sector_count <= 0)
    {
        return SDL_InvalidParamError("sectors");
    }
    if (geometry == NULL)
    {
        return SDL_InvalidParamError("geometry");
    }
    if (materials == NULL || material_count <= 0)
    {
        return SDL_InvalidParamError("materials");
    }
    if (level->sector_count != sector_count)
    {
        return SDL_SetError("sector_count must match the built level.");
    }
    if (level->material_count != material_count || level->model.material_count != material_count)
    {
        return SDL_SetError("material_count must match the built level.");
    }
    if (sector_index < 0 || sector_index >= sector_count)
    {
        return SDL_SetError("sector_index is out of range.");
    }
    if (geometry->ceil_y <= geometry->floor_y + SDL3D_LEVEL_PLANE_EPSILON)
    {
        return SDL_SetError("sector ceiling must be above sector floor.");
    }

    candidate_sectors = SDL_malloc((size_t)sector_count * sizeof(*candidate_sectors));
    if (candidate_sectors == NULL)
    {
        return SDL_OutOfMemory();
    }

    SDL_memcpy(candidate_sectors, sectors, (size_t)sector_count * sizeof(*candidate_sectors));
    sector_apply_runtime_geometry(&candidate_sectors[sector_index], geometry);

    edges = build_level_edges(candidate_sectors, sector_count, &edge_count);
    if (edges == NULL)
    {
        SDL_free(candidate_sectors);
        return false;
    }

    dirty = SDL_calloc((size_t)sector_count, sizeof(*dirty));
    acc_count = sector_count * material_count;
    accs = SDL_calloc((size_t)acc_count, sizeof(*accs));
    if (dirty == NULL || accs == NULL)
    {
        SDL_free(candidate_sectors);
        SDL_free(edges);
        SDL_free(dirty);
        SDL_free(accs);
        return SDL_OutOfMemory();
    }

    if (!mark_dirty_sector_neighbors(edges, edge_count, sector_index, dirty) ||
        !build_dirty_sector_accs(candidate_sectors, sector_count, edges, edge_count, dirty, materials, material_count,
                                 accs))
    {
        for (int ai = 0; ai < acc_count; ++ai)
        {
            macc_free(&accs[ai]);
        }
        SDL_free(accs);
        SDL_free(dirty);
        SDL_free(edges);
        SDL_free(candidate_sectors);
        return false;
    }

    bake_vertex_lighting_for_accs(accs, acc_count, lights, light_count);

    if (!rebuild_level_portals(candidate_sectors, edges, edge_count, &new_portals, &new_portal_count))
    {
        for (int ai = 0; ai < acc_count; ++ai)
        {
            macc_free(&accs[ai]);
        }
        SDL_free(accs);
        SDL_free(dirty);
        SDL_free(edges);
        SDL_free(candidate_sectors);
        return false;
    }

    old_mesh_count = level->model.mesh_count;
    for (int i = 0; i < old_mesh_count; ++i)
    {
        int sid = level->mesh_sector_ids[i];
        if (sid < 0 || sid >= sector_count || !dirty[sid])
        {
            new_mesh_count++;
        }
    }
    for (int s = 0; s < sector_count; ++s)
    {
        if (!dirty[s])
        {
            continue;
        }
        for (int m = 0; m < material_count; ++m)
        {
            if (accs[s * material_count + m].ic > 0)
            {
                new_mesh_count++;
            }
        }
    }

    new_meshes = SDL_calloc((size_t)new_mesh_count, sizeof(*new_meshes));
    new_mesh_sector_ids = SDL_malloc((size_t)new_mesh_count * sizeof(*new_mesh_sector_ids));
    if (new_meshes == NULL || new_mesh_sector_ids == NULL)
    {
        SDL_free(new_meshes);
        SDL_free(new_mesh_sector_ids);
        SDL_free(new_portals);
        for (int ai = 0; ai < acc_count; ++ai)
        {
            macc_free(&accs[ai]);
        }
        SDL_free(accs);
        SDL_free(dirty);
        SDL_free(edges);
        SDL_free(candidate_sectors);
        return SDL_OutOfMemory();
    }

    new_mesh_count = 0;
    for (int i = 0; i < old_mesh_count; ++i)
    {
        int sid = level->mesh_sector_ids[i];
        if (sid >= 0 && sid < sector_count && dirty[sid])
        {
            continue;
        }
        new_meshes[new_mesh_count] = level->model.meshes[i];
        new_mesh_sector_ids[new_mesh_count] = sid;
        new_mesh_count++;
    }

    baked_light = lights != NULL && light_count > 0;
    for (int s = 0; s < sector_count; ++s)
    {
        if (!dirty[s])
        {
            continue;
        }
        for (int m = 0; m < material_count; ++m)
        {
            macc *acc = &accs[s * material_count + m];
            if (acc->ic == 0)
            {
                continue;
            }
            move_acc_to_mesh(&new_meshes[new_mesh_count], acc, m, baked_light);
            new_mesh_sector_ids[new_mesh_count] = s;
            new_mesh_count++;
        }
    }

    for (int i = 0; i < old_mesh_count; ++i)
    {
        int sid = level->mesh_sector_ids[i];
        if (sid >= 0 && sid < sector_count && dirty[sid])
        {
            free_mesh_geometry(&level->model.meshes[i]);
        }
    }

    sector_apply_runtime_geometry(&sectors[sector_index], geometry);
    SDL_free(level->model.meshes);
    SDL_free(level->mesh_sector_ids);
    SDL_free(level->portals);
    level->model.meshes = new_meshes;
    level->model.mesh_count = new_mesh_count;
    level->mesh_sector_ids = new_mesh_sector_ids;
    level->portals = new_portals;
    level->portal_count = new_portal_count;

    for (int ai = 0; ai < acc_count; ++ai)
    {
        macc_free(&accs[ai]);
    }
    SDL_free(accs);
    SDL_free(dirty);
    SDL_free(edges);
    SDL_free(candidate_sectors);
    return true;
}

void sdl3d_free_level(sdl3d_level *level)
{
    if (level)
    {
        sdl3d_free_model(&level->model);
        SDL_free(level->mesh_sector_ids);
        SDL_free(level->portals);
        SDL_free(level->lightmap_pixels);
        sdl3d_free_texture(&level->lightmap_texture);
        level->mesh_sector_ids = NULL;
        level->portals = NULL;
        level->lightmap_pixels = NULL;
        level->portal_count = 0;
        level->sector_count = 0;
        level->material_count = 0;
        level->lightmap_width = 0;
        level->lightmap_height = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Sector lookup                                                       */
/* ------------------------------------------------------------------ */

static bool point_in_polygon_xz(const sdl3d_sector *sector, float x, float z)
{
    bool inside = false;
    for (int i = 0, j = sector->num_points - 1; i < sector->num_points; j = i++)
    {
        float xi = sector->points[i][0], zi = sector->points[i][1];
        float xj = sector->points[j][0], zj = sector->points[j][1];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi))
            inside = !inside;
    }
    return inside;
}

int sdl3d_level_find_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z)
{
    if (!level || !sectors)
        return -1;
    for (int i = 0; i < level->sector_count; i++)
    {
        if (point_in_polygon_xz(&sectors[i], x, z))
            return i;
    }
    return -1;
}

int sdl3d_level_find_sector_at(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z, float feet_y)
{
    int best = -1;
    float best_floor = -1e30f;

    if (!level || !sectors)
        return -1;

    for (int i = 0; i < level->sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        float floor_y;
        float ceil_y;
        if (!point_in_polygon_xz(sector, x, z))
            continue;
        floor_y = sdl3d_sector_floor_at(sector, x, z);
        ceil_y = sdl3d_sector_ceil_at(sector, x, z);
        if (floor_y > feet_y || feet_y >= ceil_y)
            continue;
        if (floor_y > best_floor)
        {
            best = i;
            best_floor = floor_y;
        }
    }
    return best;
}

int sdl3d_level_find_walkable_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z,
                                     float feet_y, float step_height, float player_height)
{
    int best = -1;
    float best_floor = -1e30f;

    if (!level || !sectors)
        return -1;

    for (int i = 0; i < level->sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        float floor_y;
        float ceil_y;
        if (!point_in_polygon_xz(sector, x, z))
            continue;
        floor_y = sdl3d_sector_floor_at(sector, x, z);
        ceil_y = sdl3d_sector_ceil_at(sector, x, z);
        if (floor_y > feet_y + step_height)
            continue;
        if (ceil_y - floor_y < player_height)
            continue;
        if (floor_y > best_floor)
        {
            best = i;
            best_floor = floor_y;
        }
    }
    return best;
}

int sdl3d_level_find_support_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z,
                                    float feet_y, float player_height)
{
    int best = -1;
    float best_floor = -1e30f;

    if (!level || !sectors)
        return -1;

    for (int i = 0; i < level->sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        float floor_y;
        float ceil_y;
        if (!point_in_polygon_xz(sector, x, z))
            continue;
        floor_y = sdl3d_sector_floor_at(sector, x, z);
        ceil_y = sdl3d_sector_ceil_at(sector, x, z);
        if (floor_y > feet_y)
            continue;
        if (ceil_y - floor_y < player_height)
            continue;
        if (floor_y > best_floor)
        {
            best = i;
            best_floor = floor_y;
        }
    }
    return best;
}

bool sdl3d_level_point_inside(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float y, float z)
{
    if (!level || !sectors)
        return false;
    for (int i = 0; i < level->sector_count; ++i)
    {
        const sdl3d_sector *sector = &sectors[i];
        if (point_in_polygon_xz(sector, x, z))
        {
            float floor_y = sdl3d_sector_floor_at(sector, x, z);
            float ceil_y = sdl3d_sector_ceil_at(sector, x, z);
            if (y < floor_y || y > ceil_y)
                continue;
            return true;
        }
    }
    return false;
}

void sdl3d_sector_watcher_init(sdl3d_sector_watcher *watcher)
{
    if (watcher == NULL)
    {
        return;
    }

    watcher->current_sector = -1;
    watcher->current_ambient_id = -1;
    watcher->entered_signal_id = SDL3D_SIGNAL_ENTERED_SECTOR;
}

bool sdl3d_sector_watcher_update(sdl3d_sector_watcher *watcher, const sdl3d_level *level, const sdl3d_sector *sectors,
                                 sdl3d_vec3 sample_position, sdl3d_signal_bus *bus)
{
    int next_sector;
    int next_ambient_id;
    int previous_sector;
    int previous_ambient_id;

    if (watcher == NULL || level == NULL || sectors == NULL)
    {
        return false;
    }

    next_sector = sdl3d_level_find_sector_at(level, sectors, sample_position.x, sample_position.z, sample_position.y);
    next_ambient_id =
        (next_sector >= 0 && next_sector < level->sector_count) ? sectors[next_sector].ambient_sound_id : -1;

    if (next_sector == watcher->current_sector)
    {
        return false;
    }

    previous_sector = watcher->current_sector;
    previous_ambient_id = watcher->current_ambient_id;
    watcher->current_sector = next_sector;
    watcher->current_ambient_id = next_ambient_id;

    if (bus != NULL)
    {
        sdl3d_properties *payload = sdl3d_properties_create();
        if (payload != NULL)
        {
            sdl3d_properties_set_int(payload, "sector_id", next_sector);
            sdl3d_properties_set_int(payload, "previous_sector_id", previous_sector);
            sdl3d_properties_set_int(payload, "ambient_sound_id", next_ambient_id);
            sdl3d_properties_set_int(payload, "previous_ambient_sound_id", previous_ambient_id);
            sdl3d_signal_emit(bus, watcher->entered_signal_id, payload);
            sdl3d_properties_destroy(payload);
        }
    }

    return true;
}

void sdl3d_extract_frustum_planes(sdl3d_mat4 view_projection, float out_planes[6][4])
{
    const float *m = view_projection.m;
    float raw[6][4] = {
        {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]},
        {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]},
        {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]},
        {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]},
        {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]},
        {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]},
    };
    for (int i = 0; i < 6; ++i)
    {
        float len = SDL_sqrtf(raw[i][0] * raw[i][0] + raw[i][1] * raw[i][1] + raw[i][2] * raw[i][2]);
        if (len > 1e-6f)
        {
            out_planes[i][0] = raw[i][0] / len;
            out_planes[i][1] = raw[i][1] / len;
            out_planes[i][2] = raw[i][2] / len;
            out_planes[i][3] = raw[i][3] / len;
        }
        else
        {
            out_planes[i][0] = 0.0f;
            out_planes[i][1] = 0.0f;
            out_planes[i][2] = 0.0f;
            out_planes[i][3] = raw[i][3];
        }
    }
}

sdl3d_color sdl3d_level_sample_light(const sdl3d_level_light *lights, int light_count, sdl3d_vec3 position)
{
    float r = 0.0f, g = 0.0f, b = 0.0f;

    if (lights == NULL || light_count <= 0)
    {
        return (sdl3d_color){0, 0, 0, 255};
    }

    for (int i = 0; i < light_count; ++i)
    {
        float dx = lights[i].position[0] - position.x;
        float dy = lights[i].position[1] - position.y;
        float dz = lights[i].position[2] - position.z;
        float dist = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        if (lights[i].range <= 0.0f || dist >= lights[i].range || dist <= 1e-4f)
            continue;
        float t = 1.0f - dist / lights[i].range;
        float scale = lights[i].intensity * t * t;
        r += lights[i].color[0] * scale;
        g += lights[i].color[1] * scale;
        b += lights[i].color[2] * scale;
    }

    if (r > 1.0f)
        r = 1.0f;
    if (g > 1.0f)
        g = 1.0f;
    if (b > 1.0f)
        b = 1.0f;
    if (r < 0.0f)
        r = 0.0f;
    if (g < 0.0f)
        g = 0.0f;
    if (b < 0.0f)
        b = 0.0f;

    return (sdl3d_color){(Uint8)(r * 255.0f), (Uint8)(g * 255.0f), (Uint8)(b * 255.0f), 255};
}

/* ------------------------------------------------------------------ */
/* Portal visibility traversal                                         */
/* ------------------------------------------------------------------ */

static bool portal_box_in_frustum(const sdl3d_level_portal *p, float planes[6][4])
{
    for (int i = 0; i < 6; ++i)
    {
        float a = planes[i][0], b = planes[i][1], c = planes[i][2], d = planes[i][3];
        float px = a >= 0.0f ? p->max_x : p->min_x;
        float py = b >= 0.0f ? p->ceil_y : p->floor_y;
        float pz = c >= 0.0f ? p->max_z : p->min_z;
        if (a * px + b * py + c * pz + d < 0.0f)
            return false;
    }
    return true;
}

static bool portal_behind_camera(const sdl3d_level_portal *p, sdl3d_vec3 cam_pos, sdl3d_vec3 cam_dir)
{
    /* Test portal center against camera forward half-space. */
    float cx = (p->min_x + p->max_x) * 0.5f - cam_pos.x;
    float cy = (p->floor_y + p->ceil_y) * 0.5f - cam_pos.y;
    float cz = (p->min_z + p->max_z) * 0.5f - cam_pos.z;
    float dot = cx * cam_dir.x + cy * cam_dir.y + cz * cam_dir.z;
    if (dot >= 0.0f)
        return false;

    /* Also test all 8 corners — portal is only behind if ALL corners are. */
    float xs[2] = {p->min_x, p->max_x};
    float ys[2] = {p->floor_y, p->ceil_y};
    float zs[2] = {p->min_z, p->max_z};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++)
            {
                float dx = xs[i] - cam_pos.x;
                float dy = ys[j] - cam_pos.y;
                float dz = zs[k] - cam_pos.z;
                if (dx * cam_dir.x + dy * cam_dir.y + dz * cam_dir.z >= 0.0f)
                    return false;
            }
    return true;
}

void sdl3d_level_compute_visibility(const sdl3d_level *level, int current_sector, sdl3d_vec3 camera_pos,
                                    sdl3d_vec3 camera_dir, float frustum_planes[6][4], sdl3d_visibility_result *result)
{
    if (!level || !result || !result->sector_visible)
        return;

    for (int i = 0; i < level->sector_count; i++)
        result->sector_visible[i] = false;
    result->visible_count = 0;

    if (current_sector < 0 || current_sector >= level->sector_count)
    {
        /* Outside all sectors — mark everything visible as fallback. */
        for (int i = 0; i < level->sector_count; i++)
            result->sector_visible[i] = true;
        result->visible_count = level->sector_count;
        return;
    }

    /* BFS from current sector through portals. */
    int *queue = SDL_malloc((size_t)level->sector_count * sizeof(int));
    if (!queue)
    {
        /* OOM fallback: show everything. */
        for (int i = 0; i < level->sector_count; i++)
            result->sector_visible[i] = true;
        result->visible_count = level->sector_count;
        return;
    }

    int head = 0, tail = 0;
    result->sector_visible[current_sector] = true;
    result->visible_count = 1;
    queue[tail++] = current_sector;

    while (head < tail)
    {
        int s = queue[head++];
        for (int pi = 0; pi < level->portal_count; pi++)
        {
            const sdl3d_level_portal *p = &level->portals[pi];
            int neighbor = -1;
            if (p->sector_a == s)
                neighbor = p->sector_b;
            else if (p->sector_b == s)
                neighbor = p->sector_a;
            else
                continue;

            if (result->sector_visible[neighbor])
                continue;

            /* Reject portals fully behind camera. */
            if (portal_behind_camera(p, camera_pos, camera_dir))
                continue;

            /* Reject portals outside frustum. */
            if (frustum_planes && !portal_box_in_frustum(p, frustum_planes))
                continue;

            result->sector_visible[neighbor] = true;
            result->visible_count++;
            queue[tail++] = neighbor;
        }
    }

    SDL_free(queue);
}

void sdl3d_level_compute_visibility_from_camera(const sdl3d_level *level, const sdl3d_sector *sectors,
                                                const sdl3d_camera3d *camera, int backbuffer_width,
                                                int backbuffer_height, float near_plane, float far_plane,
                                                sdl3d_visibility_result *result)
{
    if (!level || !sectors || !camera || !result || !result->sector_visible)
        return;

    /* Mark everything visible as fallback. */
    for (int i = 0; i < level->sector_count; ++i)
        result->sector_visible[i] = true;
    result->visible_count = level->sector_count;

    sdl3d_mat4 view, proj;
    if (!sdl3d_camera3d_compute_matrices(camera, backbuffer_width, backbuffer_height, near_plane, far_plane, &view,
                                         &proj))
        return;

    sdl3d_mat4 vp = sdl3d_mat4_multiply(proj, view);
    float fp[6][4];
    sdl3d_extract_frustum_planes(vp, fp);

    sdl3d_vec3 dir = sdl3d_vec3_make(camera->target.x - camera->position.x, camera->target.y - camera->position.y,
                                     camera->target.z - camera->position.z);

    int current =
        sdl3d_level_find_sector_at(level, sectors, camera->position.x, camera->position.z, camera->position.y);

    sdl3d_level_compute_visibility(level, current, camera->position, dir, fp, result);
}

sdl3d_level_trace_result sdl3d_level_trace_point(const sdl3d_level *level, const sdl3d_sector *sectors,
                                                 sdl3d_vec3 origin, sdl3d_vec3 direction, float max_distance)
{
    sdl3d_level_trace_result r;
    r.hit = false;
    r.end_point = origin;
    r.end_sector = -1;
    r.fraction = 0.0f;

    if (!level || !sectors || max_distance <= 0.0f)
        return r;

    float len = SDL_sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len < 1e-6f)
        return r;
    float inv_len = 1.0f / len;
    float dx = direction.x * inv_len;
    float dy = direction.y * inv_len;
    float dz = direction.z * inv_len;

    const float step_size = 0.25f;
    int steps = (int)SDL_ceilf(max_distance / step_size);
    if (steps < 1)
        steps = 1;
    float step_dist = max_distance / (float)steps;

    float px = origin.x, py = origin.y, pz = origin.z;
    for (int i = 0; i < steps; ++i)
    {
        float nx = px + dx * step_dist;
        float ny = py + dy * step_dist;
        float nz = pz + dz * step_dist;
        if (!sdl3d_level_point_inside(level, sectors, nx, ny, nz))
        {
            r.hit = true;
            r.end_point = sdl3d_vec3_make(px, py, pz);
            r.fraction = (float)(i) / (float)steps;
            r.end_sector = sdl3d_level_find_sector_at(level, sectors, px, pz, py);
            return r;
        }
        px = nx;
        py = ny;
        pz = nz;
    }

    r.hit = false;
    r.end_point = sdl3d_vec3_make(px, py, pz);
    r.fraction = 1.0f;
    r.end_sector = sdl3d_level_find_sector_at(level, sectors, px, pz, py);
    return r;
}

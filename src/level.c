/*
 * Sector-based level builder.
 *
 * Generates a single watertight mesh from 2D sector definitions.
 * Shared edges between sectors become doorways. All other edges
 * become walls. Floors and ceilings are triangulated per sector.
 */

#include "sdl3d/level.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Shared edge detection                                               */
/* ------------------------------------------------------------------ */

typedef struct
{
    float ax, az, bx, bz; /* edge endpoints in XZ */
    int sector;           /* which sector owns this edge */
    int edge;             /* edge index within sector */
    bool shared;          /* true if another sector shares this edge */
    float other_floor_y;  /* floor of the adjacent sector */
    float other_ceil_y;   /* ceiling of the adjacent sector */
} edge_info;

static bool edges_match(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz)
{
    /* Two edges match if they share the same endpoints (in either order). */
    float eps = 0.001f;
    bool fwd = fabsf(ax - cx) < eps && fabsf(az - cz) < eps && fabsf(bx - dx) < eps && fabsf(bz - dz) < eps;
    bool rev = fabsf(ax - dx) < eps && fabsf(az - dz) < eps && fabsf(bx - cx) < eps && fabsf(bz - cz) < eps;
    return fwd || rev;
}

/* ------------------------------------------------------------------ */
/* Mesh accumulator                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    float *positions; /* 3 floats per vertex */
    float *normals;   /* 3 floats per vertex */
    float *colors;    /* 4 floats per vertex (RGBA from material albedo) */
    unsigned int *indices;
    int vert_count;
    int vert_cap;
    int idx_count;
    int idx_cap;
    int mat_index; /* current material index for grouping */
} mesh_acc;

static bool acc_ensure_verts(mesh_acc *a, int need)
{
    if (a->vert_count + need <= a->vert_cap)
        return true;
    int cap = a->vert_cap * 2;
    if (cap < a->vert_count + need)
        cap = a->vert_count + need;
    float *p = SDL_realloc(a->positions, (size_t)cap * 3 * sizeof(float));
    float *n = SDL_realloc(a->normals, (size_t)cap * 3 * sizeof(float));
    float *c = SDL_realloc(a->colors, (size_t)cap * 4 * sizeof(float));
    if (!p || !n || !c)
        return false;
    a->positions = p;
    a->normals = n;
    a->colors = c;
    a->vert_cap = cap;
    return true;
}

static bool acc_ensure_idx(mesh_acc *a, int need)
{
    if (a->idx_count + need <= a->idx_cap)
        return true;
    int cap = a->idx_cap * 2;
    if (cap < a->idx_count + need)
        cap = a->idx_count + need;
    unsigned int *idx = SDL_realloc(a->indices, (size_t)cap * sizeof(unsigned int));
    if (!idx)
        return false;
    a->indices = idx;
    a->idx_cap = cap;
    return true;
}

static int acc_add_vert(mesh_acc *a, float x, float y, float z, float nx, float ny, float nz, const float *rgba)
{
    if (!acc_ensure_verts(a, 1))
        return -1;
    int i = a->vert_count++;
    a->positions[i * 3 + 0] = x;
    a->positions[i * 3 + 1] = y;
    a->positions[i * 3 + 2] = z;
    a->normals[i * 3 + 0] = nx;
    a->normals[i * 3 + 1] = ny;
    a->normals[i * 3 + 2] = nz;
    a->colors[i * 4 + 0] = rgba[0];
    a->colors[i * 4 + 1] = rgba[1];
    a->colors[i * 4 + 2] = rgba[2];
    a->colors[i * 4 + 3] = rgba[3];
    return i;
}

static bool acc_add_tri(mesh_acc *a, int i0, int i1, int i2)
{
    if (!acc_ensure_idx(a, 3))
        return false;
    a->indices[a->idx_count++] = (unsigned int)i0;
    a->indices[a->idx_count++] = (unsigned int)i1;
    a->indices[a->idx_count++] = (unsigned int)i2;
    return true;
}

/* Add a quad (two triangles) with a given normal and color. */
static bool acc_add_quad(mesh_acc *a, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2,
                         float z2, float x3, float y3, float z3, float nx, float ny, float nz, const float *rgba)
{
    int v0 = acc_add_vert(a, x0, y0, z0, nx, ny, nz, rgba);
    int v1 = acc_add_vert(a, x1, y1, z1, nx, ny, nz, rgba);
    int v2 = acc_add_vert(a, x2, y2, z2, nx, ny, nz, rgba);
    int v3 = acc_add_vert(a, x3, y3, z3, nx, ny, nz, rgba);
    if (v0 < 0)
        return false;
    return acc_add_tri(a, v0, v1, v2) && acc_add_tri(a, v0, v2, v3);
}

static void acc_free(mesh_acc *a)
{
    SDL_free(a->positions);
    SDL_free(a->normals);
    SDL_free(a->colors);
    SDL_free(a->indices);
    SDL_zerop(a);
}

/* ------------------------------------------------------------------ */
/* Floor/ceiling triangulation (ear clipping for convex polygons)       */
/* ------------------------------------------------------------------ */

static bool add_floor_or_ceil(mesh_acc *a, const sdl3d_sector *s, float y, float ny, const float *rgba)
{
    /* Fan triangulation from vertex 0 — works for convex polygons. */
    int n = s->num_points;
    if (n < 3)
        return true;

    int base = a->vert_count;
    for (int i = 0; i < n; i++)
    {
        if (acc_add_vert(a, s->points[i][0], y, s->points[i][1], 0, ny, 0, rgba) < 0)
            return false;
    }

    for (int i = 1; i < n - 1; i++)
    {
        if (ny > 0)
        {
            /* Floor: CCW when viewed from above */
            if (!acc_add_tri(a, base, base + i, base + i + 1))
                return false;
        }
        else
        {
            /* Ceiling: CW when viewed from above = CCW from below */
            if (!acc_add_tri(a, base, base + i + 1, base + i))
                return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Wall generation                                                     */
/* ------------------------------------------------------------------ */

static bool add_wall(mesh_acc *a, float x0, float z0, float x1, float z1, float bottom, float top, const float *rgba)
{
    /* Wall normal: perpendicular to edge, pointing inward (left of edge direction). */
    float dx = x1 - x0;
    float dz = z1 - z0;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f)
        return true;
    /* Inward normal for CCW polygon: rotate edge 90° clockwise */
    float nx = dz / len;
    float nz = -dx / len;

    /* Quad: bottom-left, bottom-right, top-right, top-left */
    return acc_add_quad(a, x0, bottom, z0, x1, bottom, z1, x1, top, z1, x0, top, z0, nx, 0, nz, rgba);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool sdl3d_build_level(const sdl3d_sector *sectors, int sector_count, const sdl3d_level_material *materials,
                       int material_count, sdl3d_level *out)
{
    if (!sectors || sector_count <= 0 || !out)
        return SDL_InvalidParamError("sectors");
    if (!materials || material_count <= 0)
        return SDL_InvalidParamError("materials");

    SDL_zerop(out);

    /* Count total edges and build edge list. */
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
            edges[ei].edge = e;
            edges[ei].shared = false;
            ei++;
        }
    }

    /* Detect shared edges. */
    for (int i = 0; i < total_edges; i++)
    {
        for (int j = i + 1; j < total_edges; j++)
        {
            if (edges[i].sector == edges[j].sector)
                continue;
            if (edges_match(edges[i].ax, edges[i].az, edges[i].bx, edges[i].bz, edges[j].ax, edges[j].az, edges[j].bx,
                            edges[j].bz))
            {
                edges[i].shared = true;
                edges[j].shared = true;
                edges[i].other_floor_y = sectors[edges[j].sector].floor_y;
                edges[i].other_ceil_y = sectors[edges[j].sector].ceil_y;
                edges[j].other_floor_y = sectors[edges[i].sector].floor_y;
                edges[j].other_ceil_y = sectors[edges[i].sector].ceil_y;
            }
        }
    }

    /* Build mesh. */
    mesh_acc acc;
    SDL_zerop(&acc);
    acc.vert_cap = 256;
    acc.idx_cap = 512;
    acc.positions = SDL_malloc((size_t)acc.vert_cap * 3 * sizeof(float));
    acc.normals = SDL_malloc((size_t)acc.vert_cap * 3 * sizeof(float));
    acc.colors = SDL_malloc((size_t)acc.vert_cap * 4 * sizeof(float));
    acc.indices = SDL_malloc((size_t)acc.idx_cap * sizeof(unsigned int));
    if (!acc.positions || !acc.normals || !acc.colors || !acc.indices)
    {
        acc_free(&acc);
        SDL_free(edges);
        return SDL_OutOfMemory();
    }

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        int fi = sec->floor_material < material_count ? sec->floor_material : 0;
        int ci = sec->ceil_material < material_count ? sec->ceil_material : 0;
        int wi = sec->wall_material < material_count ? sec->wall_material : 0;

        /* Floor (normal up) */
        if (!add_floor_or_ceil(&acc, sec, sec->floor_y, 1.0f, materials[fi].albedo))
        {
            acc_free(&acc);
            SDL_free(edges);
            return false;
        }

        /* Ceiling (normal down) */
        if (!add_floor_or_ceil(&acc, sec, sec->ceil_y, -1.0f, materials[ci].albedo))
        {
            acc_free(&acc);
            SDL_free(edges);
            return false;
        }

        /* Walls — only on non-shared edges. */
        for (int e = 0; e < total_edges; e++)
        {
            if (edges[e].sector != s)
                continue;

            if (!edges[e].shared)
            {
                /* Full wall from floor to ceiling. */
                if (!add_wall(&acc, edges[e].ax, edges[e].az, edges[e].bx, edges[e].bz, sec->floor_y, sec->ceil_y,
                              materials[wi].albedo))
                {
                    acc_free(&acc);
                    SDL_free(edges);
                    return false;
                }
            }
            else
            {
                /* Shared edge: generate step walls if floor/ceiling heights differ. */
                float my_floor = sec->floor_y;
                float my_ceil = sec->ceil_y;
                float other_floor = edges[e].other_floor_y;
                float other_ceil = edges[e].other_ceil_y;

                /* Lower wall: if our floor is lower than neighbor's floor */
                if (my_floor < other_floor - 0.001f)
                {
                    if (!add_wall(&acc, edges[e].ax, edges[e].az, edges[e].bx, edges[e].bz, my_floor, other_floor,
                                  materials[wi].albedo))
                    {
                        acc_free(&acc);
                        SDL_free(edges);
                        return false;
                    }
                }

                /* Upper wall: if our ceiling is higher than neighbor's ceiling */
                if (my_ceil > other_ceil + 0.001f)
                {
                    if (!add_wall(&acc, edges[e].ax, edges[e].az, edges[e].bx, edges[e].bz, other_ceil, my_ceil,
                                  materials[wi].albedo))
                    {
                        acc_free(&acc);
                        SDL_free(edges);
                        return false;
                    }
                }
            }
        }
    }

    SDL_free(edges);

    /* Convert accumulator to sdl3d_model with one mesh. */
    sdl3d_mesh *mesh = SDL_calloc(1, sizeof(sdl3d_mesh));
    if (!mesh)
    {
        acc_free(&acc);
        return SDL_OutOfMemory();
    }

    mesh->vertex_count = acc.vert_count;
    mesh->index_count = acc.idx_count;
    mesh->positions = acc.positions;
    mesh->normals = acc.normals;
    mesh->colors = acc.colors;
    mesh->indices = acc.indices;
    mesh->material_index = 0;

    /* Create a default material from the first level material. */
    sdl3d_material *mat = SDL_calloc(1, sizeof(sdl3d_material));
    if (!mat)
    {
        SDL_free(mesh);
        acc_free(&acc);
        return SDL_OutOfMemory();
    }
    mat->albedo[0] = 1.0f;
    mat->albedo[1] = 1.0f;
    mat->albedo[2] = 1.0f;
    mat->albedo[3] = 1.0f;
    mat->roughness = 0.8f;

    out->model.meshes = mesh;
    out->model.mesh_count = 1;
    out->model.materials = mat;
    out->model.material_count = 1;

    SDL_Log("SDL3D level: %d verts, %d tris from %d sectors", acc.vert_count, acc.idx_count / 3, sector_count);
    return true;
}

void sdl3d_free_level(sdl3d_level *level)
{
    if (level)
    {
        sdl3d_free_model(&level->model);
    }
}

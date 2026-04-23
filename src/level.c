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
#include <math.h>

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
    return fabsf(cross1) < eps && fabsf(cross2) < eps;
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
    float other_floor, other_ceil;
} overlap_t;

/* ------------------------------------------------------------------ */
/* Mesh accumulator                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
    float *pos;
    float *nrm;
    float *col;
    float *uvs;
    unsigned int *idx;
    int vc, vc_cap;
    int ic, ic_cap;
} macc;

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
    SDL_free(a->idx);
}

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                     */
/* ------------------------------------------------------------------ */

static bool add_wall(macc *a, float x0, float z0, float x1, float z1, float bot, float top, const float *rgba,
                     float tex_scale)
{
    if (top - bot < 0.001f)
        return true;
    float dx = x1 - x0, dz = z1 - z0;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f)
        return true;
    float nx = dz / len, nz = -dx / len;
    float s = tex_scale > 0 ? tex_scale : 4.0f;
    float u0 = 0.0f, u1 = len / s;
    float v0 = bot / s, v1 = top / s;
    int v_0 = macc_vert(a, x0, bot, z0, nx, 0, nz, rgba, u0, v0);
    int v_1 = macc_vert(a, x1, bot, z1, nx, 0, nz, rgba, u1, v0);
    int v_2 = macc_vert(a, x1, top, z1, nx, 0, nz, rgba, u1, v1);
    int v_3 = macc_vert(a, x0, top, z0, nx, 0, nz, rgba, u0, v1);
    if (v_0 < 0)
        return false;
    return macc_tri(a, v_0, v_1, v_2) && macc_tri(a, v_0, v_2, v_3);
}

static bool add_floor_ceil(macc *a, const sdl3d_sector *s, float y, float ny, const float *rgba, float tex_scale)
{
    int n = s->num_points;
    if (n < 3)
        return true;
    int base = a->vc;
    float sc = tex_scale > 0 ? tex_scale : 4.0f;
    for (int i = 0; i < n; i++)
    {
        if (macc_vert(a, s->points[i][0], y, s->points[i][1], 0, ny, 0, rgba, s->points[i][0] / sc,
                      s->points[i][1] / sc) < 0)
            return false;
    }
    for (int i = 1; i < n - 1; i++)
    {
        if (ny > 0)
        {
            if (!macc_tri(a, base, base + i, base + i + 1))
                return false;
        }
        else
        {
            if (!macc_tri(a, base, base + i + 1, base + i))
                return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool sdl3d_build_level(const sdl3d_sector *sectors, int sector_count, const sdl3d_level_material *materials,
                       int material_count, const sdl3d_level_light *lights, int light_count, sdl3d_level *out)
{
    if (!sectors || sector_count <= 0 || !out)
        return SDL_InvalidParamError("sectors");
    if (!materials || material_count <= 0)
        return SDL_InvalidParamError("materials");

    SDL_zerop(out);

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

    /* Per-material mesh accumulators. */
    macc *accs = SDL_calloc((size_t)material_count, sizeof(macc));
    if (!accs)
    {
        SDL_free(edges);
        return SDL_OutOfMemory();
    }
    for (int m = 0; m < material_count; m++)
    {
        accs[m].vc_cap = 64;
        accs[m].ic_cap = 128;
        accs[m].pos = SDL_malloc((size_t)accs[m].vc_cap * 3 * sizeof(float));
        accs[m].nrm = SDL_malloc((size_t)accs[m].vc_cap * 3 * sizeof(float));
        accs[m].col = SDL_malloc((size_t)accs[m].vc_cap * 4 * sizeof(float));
        accs[m].uvs = SDL_malloc((size_t)accs[m].vc_cap * 2 * sizeof(float));
        accs[m].idx = SDL_malloc((size_t)accs[m].ic_cap * sizeof(unsigned int));
    }

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        int fi = sec->floor_material < material_count ? sec->floor_material : 0;
        int ci = sec->ceil_material < material_count ? sec->ceil_material : 0;
        int wi = sec->wall_material < material_count ? sec->wall_material : 0;

        add_floor_ceil(&accs[fi], sec, sec->floor_y, 1.0f, materials[fi].albedo, materials[fi].tex_scale);
        add_floor_ceil(&accs[ci], sec, sec->ceil_y, -1.0f, materials[ci].albedo, materials[ci].tex_scale);

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
                    overlaps[novl].other_floor = sectors[edges[j].sector].floor_y;
                    overlaps[novl].other_ceil = sectors[edges[j].sector].ceil_y;
                    novl++;
                }
            }

            if (novl == 0)
            {
                add_wall(&accs[wi], ax, az, bx, bz, sec->floor_y, sec->ceil_y, materials[wi].albedo,
                         materials[wi].tex_scale);
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
                        add_wall(&accs[wi], wx0, wz0, wx1, wz1, sec->floor_y, sec->ceil_y, materials[wi].albedo,
                                 materials[wi].tex_scale);
                    }
                    float ox0 = ax + (bx - ax) * overlaps[oi].t0, oz0 = az + (bz - az) * overlaps[oi].t0;
                    float ox1 = ax + (bx - ax) * overlaps[oi].t1, oz1 = az + (bz - az) * overlaps[oi].t1;
                    if (sec->floor_y < overlaps[oi].other_floor - 0.001f)
                        add_wall(&accs[wi], ox0, oz0, ox1, oz1, sec->floor_y, overlaps[oi].other_floor,
                                 materials[wi].albedo, materials[wi].tex_scale);
                    if (sec->ceil_y > overlaps[oi].other_ceil + 0.001f)
                        add_wall(&accs[wi], ox0, oz0, ox1, oz1, overlaps[oi].other_ceil, sec->ceil_y,
                                 materials[wi].albedo, materials[wi].tex_scale);
                    cursor = overlaps[oi].t1;
                }
                if (cursor < 1.0f - 0.001f)
                {
                    float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                    add_wall(&accs[wi], wx0, wz0, bx, bz, sec->floor_y, sec->ceil_y, materials[wi].albedo,
                             materials[wi].tex_scale);
                }
            }
        }
    }

    SDL_free(edges);

    /* ---- Bake vertex lighting per material ---- */
    if (lights && light_count > 0)
    {
        for (int m = 0; m < material_count; m++)
        {
            macc *a = &accs[m];
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
                    float dist = sqrtf(lx * lx + ly * ly + lz * lz);
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
    }

    /* ---- Package: one mesh + one material per level material ---- */
    int num_meshes = 0;
    for (int m = 0; m < material_count; m++)
        if (accs[m].ic > 0)
            num_meshes++;

    sdl3d_mesh *meshes = SDL_calloc((size_t)num_meshes, sizeof(sdl3d_mesh));
    sdl3d_material *out_mats = SDL_calloc((size_t)num_meshes, sizeof(sdl3d_material));
    if (!meshes || !out_mats)
    {
        for (int m = 0; m < material_count; m++)
            macc_free(&accs[m]);
        SDL_free(accs);
        SDL_free(meshes);
        SDL_free(out_mats);
        return SDL_OutOfMemory();
    }

    int mi = 0;
    int total_verts = 0, total_tris = 0;
    for (int m = 0; m < material_count; m++)
    {
        if (accs[m].ic == 0)
        {
            macc_free(&accs[m]);
            continue;
        }
        meshes[mi].vertex_count = accs[m].vc;
        meshes[mi].index_count = accs[m].ic;
        meshes[mi].positions = accs[m].pos;
        meshes[mi].normals = accs[m].nrm;
        meshes[mi].colors = accs[m].col;
        meshes[mi].uvs = accs[m].uvs;
        meshes[mi].indices = accs[m].idx;
        meshes[mi].material_index = mi;

        out_mats[mi].albedo[0] = out_mats[mi].albedo[1] = out_mats[mi].albedo[2] = out_mats[mi].albedo[3] = 1.0f;
        out_mats[mi].roughness = materials[m].roughness;
        out_mats[mi].metallic = materials[m].metallic;
        if (materials[m].texture)
            out_mats[mi].albedo_map = SDL_strdup(materials[m].texture);

        total_verts += accs[m].vc;
        total_tris += accs[m].ic / 3;
        mi++;
    }
    SDL_free(accs);

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
    out->model.material_count = num_meshes;

    SDL_Log("SDL3D level: %d verts, %d tris, %d meshes from %d sectors", total_verts, total_tris, num_meshes,
            sector_count);
    return true;
}

void sdl3d_free_level(sdl3d_level *level)
{
    if (level)
        sdl3d_free_model(&level->model);
}

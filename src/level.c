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
    float other_floor, other_ceil;
    int other_sector;
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
    bool has_bounds;
    sdl3d_bounding_box bounds;
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
    SDL_free(a->idx);
}

/* ------------------------------------------------------------------ */
/* Surface tracking for lightmap UV generation                         */
/* ------------------------------------------------------------------ */

typedef struct
{
    int acc_index;    /* which macc (sector × material) */
    int first_vert;   /* first vertex index in that macc */
    int vert_count;   /* number of vertices in this surface */
    float nx, ny, nz; /* surface normal */
    /* World-space bounds of the surface (for atlas sizing). */
    float world_min[3], world_max[3];
} lm_surface;

typedef struct
{
    lm_surface *surfaces;
    int count, cap;
} lm_surface_list;

static void lm_surface_list_init(lm_surface_list *l)
{
    l->surfaces = NULL;
    l->count = 0;
    l->cap = 0;
}

static lm_surface *lm_surface_list_add(lm_surface_list *l)
{
    if (l->count == l->cap)
    {
        int c = l->cap ? l->cap * 2 : 64;
        lm_surface *s = SDL_realloc(l->surfaces, (size_t)c * sizeof(lm_surface));
        if (!s)
            return NULL;
        l->surfaces = s;
        l->cap = c;
    }
    lm_surface *s = &l->surfaces[l->count++];
    SDL_memset(s, 0, sizeof(*s));
    return s;
}

static void lm_surface_list_free(lm_surface_list *l)
{
    SDL_free(l->surfaces);
    l->surfaces = NULL;
    l->count = l->cap = 0;
}

/* Record a surface from vertices [first_vert, macc.vc) in accumulator acc_index. */
static void lm_record_surface(lm_surface_list *sl, int acc_index, const macc *a, int first_vert, float snx, float sny,
                              float snz)
{
    int nv = a->vc - first_vert;
    if (nv < 3)
        return;
    lm_surface *s = lm_surface_list_add(sl);
    if (!s)
        return;
    s->acc_index = acc_index;
    s->first_vert = first_vert;
    s->vert_count = nv;
    s->nx = snx;
    s->ny = sny;
    s->nz = snz;
    /* Compute world bounds. */
    s->world_min[0] = s->world_min[1] = s->world_min[2] = 1e30f;
    s->world_max[0] = s->world_max[1] = s->world_max[2] = -1e30f;
    for (int i = first_vert; i < a->vc; i++)
    {
        for (int c = 0; c < 3; c++)
        {
            float v = a->pos[i * 3 + c];
            if (v < s->world_min[c])
                s->world_min[c] = v;
            if (v > s->world_max[c])
                s->world_max[c] = v;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Suppress unused warnings until full lightmap baking is wired up. */
SDL_COMPILE_TIME_ASSERT(lm_surface_size, sizeof(lm_surface) > 0);
static const void *lm_unused_[] = {(void *)lm_record_surface, (void *)lm_surface_list_free, (void *)lm_unused_};

/* Geometry helpers                                                     */
/* ------------------------------------------------------------------ */

static bool add_wall(macc *a, float x0, float z0, float x1, float z1, float bot, float top, const float *rgba,
                     float tex_scale)
{
    if (top - bot < 0.001f)
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

    lm_surface_list surf_list;
    lm_surface_list_init(&surf_list);

    for (int s = 0; s < sector_count; s++)
    {
        const sdl3d_sector *sec = &sectors[s];
        int fi = (sec->floor_material >= 0 && sec->floor_material < material_count) ? sec->floor_material : -1;
        int ci = (sec->ceil_material >= 0 && sec->ceil_material < material_count) ? sec->ceil_material : -1;
        int wi = sec->wall_material < material_count ? sec->wall_material : 0;
        macc *wall_acc = &accs[s * material_count + wi];

        if (fi >= 0)
        {
            int ai = s * material_count + fi;
            macc *floor_acc = &accs[ai];
            int vb = floor_acc->vc;
            add_floor_ceil(floor_acc, sec, sec->floor_y, 1.0f, materials[fi].albedo, materials[fi].tex_scale);
            lm_record_surface(&surf_list, ai, floor_acc, vb, 0, 1, 0);
        }
        if (ci >= 0)
        {
            int ai = s * material_count + ci;
            macc *ceil_acc = &accs[ai];
            int vb = ceil_acc->vc;
            add_floor_ceil(ceil_acc, sec, sec->ceil_y, -1.0f, materials[ci].albedo, materials[ci].tex_scale);
            lm_record_surface(&surf_list, ai, ceil_acc, vb, 0, -1, 0);
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
                    overlaps[novl].other_floor = sectors[edges[j].sector].floor_y;
                    overlaps[novl].other_ceil = sectors[edges[j].sector].ceil_y;
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
                            float p_floor = sec->floor_y > overlaps[novl - 1].other_floor
                                                ? sec->floor_y
                                                : overlaps[novl - 1].other_floor;
                            float p_ceil = sec->ceil_y < overlaps[novl - 1].other_ceil ? sec->ceil_y
                                                                                       : overlaps[novl - 1].other_ceil;
                            sdl3d_level_portal *p = &portals[portal_count++];
                            p->sector_a = s;
                            p->sector_b = edges[j].sector;
                            p->min_x = px0 < px1 ? px0 : px1;
                            p->max_x = px0 > px1 ? px0 : px1;
                            p->min_z = pz0 < pz1 ? pz0 : pz1;
                            p->max_z = pz0 > pz1 ? pz0 : pz1;
                            p->floor_y = p_floor;
                            p->ceil_y = p_ceil;
                        }
                    }
                }
            }

            if (novl == 0)
            {
                add_wall(wall_acc, ax, az, bx, bz, sec->floor_y, sec->ceil_y, materials[wi].albedo,
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
                        add_wall(wall_acc, wx0, wz0, wx1, wz1, sec->floor_y, sec->ceil_y, materials[wi].albedo,
                                 materials[wi].tex_scale);
                    }
                    float ox0 = ax + (bx - ax) * overlaps[oi].t0, oz0 = az + (bz - az) * overlaps[oi].t0;
                    float ox1 = ax + (bx - ax) * overlaps[oi].t1, oz1 = az + (bz - az) * overlaps[oi].t1;
                    if (sec->floor_y < overlaps[oi].other_floor - 0.001f)
                        add_wall(wall_acc, ox0, oz0, ox1, oz1, sec->floor_y, overlaps[oi].other_floor,
                                 materials[wi].albedo, materials[wi].tex_scale);
                    if (sec->ceil_y > overlaps[oi].other_ceil + 0.001f)
                        add_wall(wall_acc, ox0, oz0, ox1, oz1, overlaps[oi].other_ceil, sec->ceil_y,
                                 materials[wi].albedo, materials[wi].tex_scale);
                    cursor = overlaps[oi].t1;
                }
                if (cursor < 1.0f - 0.001f)
                {
                    float wx0 = ax + (bx - ax) * cursor, wz0 = az + (bz - az) * cursor;
                    add_wall(wall_acc, wx0, wz0, bx, bz, sec->floor_y, sec->ceil_y, materials[wi].albedo,
                             materials[wi].tex_scale);
                }
            }
        }
    }

    SDL_free(edges);

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
        for (int ai = 0; ai < acc_count; ai++)
            macc_free(&accs[ai]);
        SDL_free(accs);
        SDL_free(meshes);
        SDL_free(out_mats);
        SDL_free(mesh_sector_ids);
        SDL_free(portals);
        return SDL_OutOfMemory();
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
    out->portal_count = portal_count;
    out->portals = portals;

    SDL_Log("SDL3D level: %d verts, %d tris, %d meshes, %d portals from %d sectors", total_verts, total_tris,
            num_meshes, portal_count, sector_count);
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
        level->mesh_sector_ids = NULL;
        level->portals = NULL;
        level->lightmap_pixels = NULL;
        level->portal_count = 0;
        level->sector_count = 0;
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

/* ------------------------------------------------------------------ */
/* Portal visibility traversal                                         */
/* ------------------------------------------------------------------ */

static bool portal_box_in_frustum(const sdl3d_level_portal *p, const float planes[6][4])
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
                                    sdl3d_vec3 camera_dir, const float frustum_planes[6][4],
                                    sdl3d_visibility_result *result)
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

/*
 * Sector-based level builder for Doom/Quake-style indoor levels.
 *
 * Define rooms as 2D floor plans with heights. The builder generates
 * a single watertight mesh with no overlapping faces — no seams,
 * no z-fighting.
 *
 * Usage:
 *   sdl3d_level_material mats[] = { ... };
 *   sdl3d_sector sectors[] = { ... };
 *   sdl3d_level level;
 *   sdl3d_build_level(sectors, count, mats, mat_count, &level);
 *   // render loop:
 *   sdl3d_draw_model(ctx, &level.model, pos, 1.0f, white);
 *   // cleanup:
 *   sdl3d_free_level(&level);
 */

#ifndef SDL3D_LEVEL_H
#define SDL3D_LEVEL_H

#include "sdl3d/model.h"
#include "sdl3d/texture.h"
#include "sdl3d/types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_SECTOR_MAX_POINTS 32

    struct sdl3d_render_context;

    typedef struct sdl3d_level_material
    {
        float albedo[4];     /* RGBA; default {1,1,1,1} */
        float metallic;      /* [0,1]; default 0 */
        float roughness;     /* [0,1]; default 1 */
        const char *texture; /* path to diffuse texture, or NULL */
        float tex_scale;     /* UV scale (units per repeat); 0 = default (4) */
    } sdl3d_level_material;

    typedef struct sdl3d_sector
    {
        float points[SDL3D_SECTOR_MAX_POINTS][2]; /* XZ polygon vertices, CCW */
        int num_points;
        float floor_y;
        float ceil_y;
        int floor_material; /* index into material palette, or -1 to omit floor geometry */
        int ceil_material;  /* index into material palette, or -1 to omit ceiling geometry */
        int wall_material;
    } sdl3d_sector;

    typedef struct sdl3d_level
    {
        sdl3d_model model;

        /* Runtime sector/portal metadata populated by sdl3d_build_level. */
        int sector_count;

        /* Per-mesh sector ownership: mesh_sector_ids[mesh_index] = sector index.
         * Length = model.mesh_count. */
        int *mesh_sector_ids;

        /* Portal adjacency graph. */
        int portal_count;
        struct sdl3d_level_portal *portals;

        /* Lightmap atlas (NULL when no lightmap baked). */
        unsigned char *lightmap_pixels; /* RGB, lightmap_width × lightmap_height */
        int lightmap_width;
        int lightmap_height;
        sdl3d_texture2d lightmap_texture; /* RGBA upload texture, zero-initialized when absent */
    } sdl3d_level;

    /* A portal connecting two adjacent sectors through a shared edge opening. */
    typedef struct sdl3d_level_portal
    {
        int sector_a;
        int sector_b;
        float min_x, max_x; /* XZ span of the opening */
        float min_z, max_z;
        float floor_y; /* opening bottom */
        float ceil_y;  /* opening top */
    } sdl3d_level_portal;

    /* Visibility query result. */
    typedef struct sdl3d_visibility_result
    {
        bool *sector_visible; /* caller-provided array[sector_count] */
        int visible_count;    /* number of visible sectors */
    } sdl3d_visibility_result;

    typedef struct sdl3d_level_light
    {
        float position[3]; /* world XYZ */
        float color[3];    /* RGB, 0-1 */
        float intensity;
        float range;
    } sdl3d_level_light;

    /*
     * Build a watertight mesh from sector definitions.
     * Shared edges between sectors become doorways (no wall generated).
     * If lights are provided, vertex lighting is baked into vertex colors
     * for fallback compatibility and a lightmap atlas is generated for
     * per-pixel baked world lighting. Pass NULL/0 for lights to use raw
     * material colors and skip lightmap generation.
     * Returns false with SDL_GetError on failure.
     */
    bool sdl3d_build_level(const sdl3d_sector *sectors, int sector_count, const sdl3d_level_material *materials,
                           int material_count, const sdl3d_level_light *lights, int light_count, sdl3d_level *out);

    void sdl3d_free_level(sdl3d_level *level);

    /*
     * Find which sector contains the given XZ point, or -1 if outside all
     * sectors. Uses 2D point-in-polygon on the sector floor plans.
     */
    int sdl3d_level_find_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z);

    /*
     * Compute which sectors are visible from the camera's current sector
     * by traversing the portal graph. Portals behind the camera or outside
     * the frustum are rejected. The caller must provide sector_visible as
     * a bool array of at least level->sector_count elements.
     *
     * camera_pos/camera_dir: world-space camera position and forward vector.
     * frustum_planes: 6 normalized frustum planes as float[6][4] (a,b,c,d),
     *                 or NULL to skip frustum rejection (all reachable sectors
     *                 are marked visible).
     *
     * Returns the number of visible sectors via result->visible_count.
     */
    void sdl3d_level_compute_visibility(const sdl3d_level *level, int current_sector, sdl3d_vec3 camera_pos,
                                        sdl3d_vec3 camera_dir, const float frustum_planes[6][4],
                                        sdl3d_visibility_result *result);

    /*
     * Draw a built level, skipping meshes whose sector is not marked visible.
     * If vis is NULL, all meshes are drawn (equivalent to sdl3d_draw_model).
     * Frustum culling still applies per-mesh as a second stage.
     */
    bool sdl3d_draw_level(struct sdl3d_render_context *context, const sdl3d_level *level,
                          const sdl3d_visibility_result *vis, sdl3d_color tint);

#ifdef __cplusplus
}
#endif

#endif

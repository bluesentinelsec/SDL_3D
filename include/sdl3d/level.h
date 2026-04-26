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

#include "sdl3d/camera.h"
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
        float floor_y;      /* Floor height at the sector centroid. */
        float ceil_y;       /* Ceiling height at the sector centroid. */
        int floor_material; /* index into material palette, or -1 to omit floor geometry */
        int ceil_material;  /* index into material palette, or -1 to omit ceiling geometry */
        int wall_material;
        float floor_normal[3]; /* Floor plane normal. Zero defaults to (0, 1, 0). */
        float ceil_normal[3];  /* Ceiling plane normal. Zero defaults to (0, -1, 0). */
    } sdl3d_sector;

    /**
     * @brief Runtime-editable vertical sector geometry.
     *
     * This describes the part of a sector that can move during gameplay:
     * the floor and ceiling plane heights at the sector centroid, plus the
     * floor and ceiling normals. The sector polygon, materials, and winding
     * remain owned by the existing sdl3d_sector definition.
     */
    typedef struct sdl3d_sector_geometry
    {
        float floor_y;         /**< Floor height at the sector centroid. */
        float ceil_y;          /**< Ceiling height at the sector centroid. */
        float floor_normal[3]; /**< Floor plane normal, or zero for flat up. */
        float ceil_normal[3];  /**< Ceiling plane normal, or zero for flat down. */
    } sdl3d_sector_geometry;

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

    /**
     * @brief Change one sector's runtime geometry and rebuild level data.
     *
     * The update is transactional for a single level: the function copies
     * `sectors`, applies `geometry` to `sector_index`, rebuilds a temporary
     * level, and swaps it into `level` only after the rebuild succeeds. On
     * success, `sectors[sector_index]` is updated to match the committed
     * geometry. On failure, the live level and caller-owned sector array are
     * left unchanged and SDL_GetError() describes the problem.
     *
     * Rebuilding the level refreshes generated floor, ceiling, wall, portal,
     * visibility, vertex lighting, and lightmap data so adjacent step walls
     * remain consistent when a floor or ceiling moves.
     *
     * @param level Built level to replace on success.
     * @param sectors Mutable sector definitions used by gameplay queries.
     * @param sector_count Number of entries in `sectors`; must match
     *        `level->sector_count`.
     * @param sector_index Sector to edit.
     * @param geometry New vertical geometry for the sector.
     * @param materials Material palette used to rebuild the level.
     * @param material_count Number of entries in `materials`.
     * @param lights Optional baked lights, or NULL with `light_count` 0.
     * @param light_count Number of entries in `lights`.
     * @return true when the new geometry was committed, false on error.
     */
    bool sdl3d_level_set_sector_geometry(sdl3d_level *level, sdl3d_sector *sectors, int sector_count, int sector_index,
                                         const sdl3d_sector_geometry *geometry, const sdl3d_level_material *materials,
                                         int material_count, const sdl3d_level_light *lights, int light_count);

    void sdl3d_free_level(sdl3d_level *level);

    /**
     * @brief Return the effective normalized floor normal for a sector.
     *
     * A sector with floor_normal set to {0, 0, 0} is treated as a flat floor
     * with normal (0, 1, 0). Safe with NULL.
     */
    sdl3d_vec3 sdl3d_sector_floor_normal(const sdl3d_sector *sector);

    /**
     * @brief Return the effective normalized ceiling normal for a sector.
     *
     * A sector with ceil_normal set to {0, 0, 0} is treated as a flat ceiling
     * with normal (0, -1, 0). Safe with NULL.
     */
    sdl3d_vec3 sdl3d_sector_ceil_normal(const sdl3d_sector *sector);

    /**
     * @brief Evaluate the sector floor plane height at a world XZ point.
     *
     * The plane passes through floor_y at the sector centroid. Flat sectors
     * therefore return floor_y for every point.
     */
    float sdl3d_sector_floor_at(const sdl3d_sector *sector, float x, float z);

    /**
     * @brief Evaluate the sector ceiling plane height at a world XZ point.
     *
     * The plane passes through ceil_y at the sector centroid. Flat sectors
     * therefore return ceil_y for every point.
     */
    float sdl3d_sector_ceil_at(const sdl3d_sector *sector, float x, float z);

    /*
     * Find which sector contains the given XZ point, or -1 if outside all
     * sectors. Uses 2D point-in-polygon on the sector floor plans.
     */
    int sdl3d_level_find_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z);

    /*
     * Find the sector containing (x, z) where feet_y is inside the sector's
     * vertical span (floor_y <= feet_y < ceil_y). Returns the sector with
     * the highest floor when multiple stacked sectors qualify (the sector
     * the player is "standing on"). Returns -1 if no sector qualifies.
     */
    int sdl3d_level_find_sector_at(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z,
                                   float feet_y);

    /*
     * Find the highest sector at (x, z) whose floor is at most step_height
     * above feet_y and that has at least player_height of headroom. Used
     * for stair climbing and walkable-position queries. The player_height
     * argument should include any ceiling clearance the caller wants
     * enforced. Returns -1 if no walkable sector exists.
     */
    int sdl3d_level_find_walkable_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z,
                                         float feet_y, float step_height, float player_height);

    /*
     * Find the highest sector at (x, z) whose floor is at or below feet_y
     * and that has at least player_height of headroom. Used for falling /
     * floor-snap queries: returns the floor the player will land on.
     * Returns -1 if no support sector exists.
     */
    int sdl3d_level_find_support_sector(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float z,
                                        float feet_y, float player_height);

    /*
     * Test whether the world-space point (x, y, z) is inside any sector
     * volume (XZ polygon plus floor_y <= y <= ceil_y). Useful for
     * projectile lifetime and trigger volume checks.
     */
    bool sdl3d_level_point_inside(const sdl3d_level *level, const sdl3d_sector *sectors, float x, float y, float z);

    /*
     * Extract 6 normalized frustum planes from a row-major view-projection
     * matrix. The output planes use the convention a*x + b*y + c*z + d >= 0
     * for the inside half-space. Order: left, right, bottom, top, near, far.
     */
    void sdl3d_extract_frustum_planes(sdl3d_mat4 view_projection, float out_planes[6][4]);

    /*
     * Sample the accumulated point-light contribution at a world-space
     * position. Each light uses quadratic distance falloff scaled by its
     * intensity. The result is clamped to sdl3d_color range and contains
     * no ambient term — callers add their own ambient floor.
     */
    sdl3d_color sdl3d_level_sample_light(const sdl3d_level_light *lights, int light_count, sdl3d_vec3 position);

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
                                        sdl3d_vec3 camera_dir, float frustum_planes[6][4],
                                        sdl3d_visibility_result *result);

    /*
     * Convenience wrapper: compute portal visibility directly from a camera.
     * Internally derives the view-projection matrix, extracts frustum planes,
     * finds the camera's current sector, and calls sdl3d_level_compute_visibility.
     * The caller must provide result->sector_visible as a bool array of at
     * least level->sector_count elements.
     */
    void sdl3d_level_compute_visibility_from_camera(const sdl3d_level *level, const sdl3d_sector *sectors,
                                                    const sdl3d_camera3d *camera, int backbuffer_width,
                                                    int backbuffer_height, float near_plane, float far_plane,
                                                    sdl3d_visibility_result *result);

    /* Result of a point trace through sector volumes. */
    typedef struct sdl3d_level_trace_result
    {
        bool hit;             /* true if the trace left all sectors          */
        sdl3d_vec3 end_point; /* final position (exit point or end of range) */
        int end_sector;       /* sector at end_point, or -1 if outside      */
        float fraction;       /* 0-1, how far along the trace               */
    } sdl3d_level_trace_result;

    /*
     * Trace a point through sector volumes from origin along direction for
     * up to max_distance. Stops when the point exits all sectors. Internally
     * substepped (0.25 unit steps) so thin sectors are not skipped.
     * Useful for projectiles, hitscan, and line-of-sight checks.
     */
    sdl3d_level_trace_result sdl3d_level_trace_point(const sdl3d_level *level, const sdl3d_sector *sectors,
                                                     sdl3d_vec3 origin, sdl3d_vec3 direction, float max_distance);

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

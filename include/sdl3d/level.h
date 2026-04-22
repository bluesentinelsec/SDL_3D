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
#include "sdl3d/types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SDL3D_SECTOR_MAX_POINTS 32

    typedef struct sdl3d_level_material
    {
        float albedo[4];   /* RGBA; default {1,1,1,1} */
        float metallic;    /* [0,1]; default 0 */
        float roughness;   /* [0,1]; default 1 */
    } sdl3d_level_material;

    typedef struct sdl3d_sector
    {
        float points[SDL3D_SECTOR_MAX_POINTS][2]; /* XZ polygon vertices, CCW */
        int num_points;
        float floor_y;
        float ceil_y;
        int floor_material; /* index into material palette */
        int ceil_material;
        int wall_material;
    } sdl3d_sector;

    typedef struct sdl3d_level
    {
        sdl3d_model model;
    } sdl3d_level;

    /*
     * Build a watertight mesh from sector definitions.
     * Shared edges between sectors become doorways (no wall generated).
     * Returns false with SDL_GetError on failure.
     */
    bool sdl3d_build_level(const sdl3d_sector *sectors, int sector_count,
                           const sdl3d_level_material *materials, int material_count,
                           sdl3d_level *out);

    void sdl3d_free_level(sdl3d_level *level);

#ifdef __cplusplus
}
#endif

#endif

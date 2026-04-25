/* Level content: sector geometry, materials, lights, and built levels. */
#include "level_data.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

/* ---- Material palette ---- */
static const sdl3d_level_material g_mats[] = {
    {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/rock_floor.jpg", 4},
    {{1, 1, 1, 1}, 0, 0.8f, SDL3D_MEDIA_DIR "/textures/ceiling_metal.jpg", 4},
    {{1, 1, 1, 1}, 0, 0.7f, SDL3D_MEDIA_DIR "/textures/wall_metal.jpg", 4},
    {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/lava.jpg", 4},
    {{1, 1, 1, 1}, 0, 0.6f, SDL3D_MEDIA_DIR "/textures/wall_metal.jpg", 4},
    {{1, 1, 1, 1}, 0, 0.9f, SDL3D_MEDIA_DIR "/textures/rock_floor.jpg", 4},
};

/*
 * Layout (top-down, Z increases downward):
 *
 *   [7] Upper Hall -------- [8] Computer Core
 *      |                          |
 *   [0] Start Room                [9] Security Bend
 *      |                               |
 *   [1] South Corridor                 |
 *      |                               |
 *   [2] Nukage Basin -- [3] East Passage -- [4] Courtyard -- [10] Storage -- [12] Secret Annex
 *      |                                       |
 *   [6] West Alcove                           [5] Exit Room -- [11] Reactor Hall -- [13] Exterior Yard
 */
const sdl3d_sector g_sectors[] = {
    {{{0, 0}, {10, 0}, {10, 8}, {0, 8}}, 4, 0.0f, 4.0f, 0, 1, 2},
    {{{3, 8}, {7, 8}, {7, 16}, {3, 16}}, 4, 0.0f, 3.5f, 5, 1, 4},
    {{{-2, 16}, {10, 16}, {10, 26}, {-2, 26}}, 4, -0.5f, 4.5f, 3, 1, 2},
    {{{10, 18}, {16, 18}, {16, 22}, {10, 22}}, 4, 0.0f, 3.5f, 0, 1, 4},
    {{{16, 14}, {28, 14}, {28, 26}, {16, 26}}, 4, 0.0f, 8.0f, 0, -1, 2},
    {{{20, 26}, {28, 26}, {28, 32}, {20, 32}}, 4, 0.0f, 3.0f, 5, 1, 4},
    {{{-10, 18}, {-2, 18}, {-2, 24}, {-10, 24}}, 4, -0.5f, 3.5f, 5, 1, 4},
    {{{10, 2}, {18, 2}, {18, 6}, {10, 6}}, 4, 0.0f, 3.5f, 5, 1, 4},
    {{{18, 0}, {30, 0}, {30, 8}, {18, 8}}, 4, 0.0f, 4.0f, 0, 1, 2},
    {{{22, 8}, {26, 8}, {26, 14}, {22, 14}}, 4, 0.0f, 3.2f, 5, 1, 4},
    {{{28, 12}, {34, 12}, {34, 24}, {28, 24}}, 4, 0.0f, 4.0f, 0, 1, 2},
    {{{18, 32}, {30, 32}, {30, 44}, {18, 44}}, 4, 0.0f, 5.5f, 5, 1, 4},
    {{{32, 24}, {40, 24}, {40, 30}, {32, 30}}, 4, 0.0f, 3.0f, 0, 1, 2},
    {{{-6, 44}, {54, 44}, {54, 104}, {-6, 104}}, 4, 0.0f, 12.0f, 5, -1, 2},
    /* Stairwell */
    {{{34, 20}, {40, 20}, {40, 24}, {34, 24}}, 4, 0.0f, 10.0f, 0, -1, 4},
    {{{34, 18}, {40, 18}, {40, 20}, {34, 20}}, 4, 1.0f, 10.0f, 0, -1, 4},
    {{{34, 16}, {40, 16}, {40, 18}, {34, 18}}, 4, 2.0f, 10.0f, 0, -1, 4},
    {{{34, 14}, {40, 14}, {40, 16}, {34, 16}}, 4, 3.0f, 10.0f, 0, -1, 4},
    {{{34, 12}, {40, 12}, {40, 14}, {34, 14}}, 4, 4.0f, 10.0f, 0, -1, 4},
    {{{34, 10}, {40, 10}, {40, 12}, {34, 12}}, 4, 5.0f, 10.0f, 0, -1, 4},
    {{{29, 4}, {40, 4}, {40, 10}, {29, 10}}, 4, 6.0f, 10.0f, 3, 1, 5},
};
const int g_sector_count = (int)SDL_arraysize(g_sectors);

const sdl3d_level_light g_lights[] = {
    {{5, 3.5f, 4}, {1.0f, 0.82f, 0.58f}, 2.4f, 9.5f},    {{5, 3.0f, 12}, {1.0f, 0.68f, 0.28f}, 1.4f, 7.0f},
    {{4, 1.0f, 21}, {1.0f, 0.16f, 0.10f}, 3.1f, 12.0f},  {{-6, 1.8f, 21}, {0.25f, 0.95f, 0.35f}, 2.4f, 8.5f},
    {{14, 2.7f, 4}, {0.7f, 0.8f, 1.0f}, 1.3f, 7.0f},     {{24, 3.2f, 4}, {0.15f, 0.85f, 1.0f}, 2.4f, 10.5f},
    {{24, 2.6f, 11}, {1.0f, 0.9f, 0.45f}, 1.4f, 6.0f},   {{22, 7.0f, 20}, {0.36f, 0.46f, 0.78f}, 2.3f, 14.0f},
    {{33, 3.0f, 18}, {0.9f, 0.35f, 1.0f}, 1.9f, 10.0f},  {{24, 2.5f, 29}, {0.2f, 1.0f, 0.2f}, 3.2f, 8.0f},
    {{24, 3.8f, 38}, {0.45f, 0.35f, 1.0f}, 2.2f, 11.0f}, {{36, 2.2f, 27}, {1.0f, 0.55f, 0.22f}, 1.5f, 7.0f},
    {{24, 8.5f, 74}, {0.32f, 0.38f, 0.7f}, 2.4f, 32.0f}, {{37, 2.5f, 22}, {1.0f, 0.8f, 0.5f}, 1.5f, 8.0f},
    {{37, 5.0f, 15}, {0.8f, 0.6f, 1.0f}, 1.8f, 10.0f},   {{34, 8.5f, 7}, {1.0f, 0.95f, 0.8f}, 3.0f, 16.0f},
};
const int g_light_count = (int)SDL_arraysize(g_lights);

static void strip_lightmap(sdl3d_level *level)
{
    SDL_free(level->lightmap_pixels);
    level->lightmap_pixels = NULL;
    level->lightmap_width = 0;
    level->lightmap_height = 0;
    sdl3d_free_texture(&level->lightmap_texture);
    for (int i = 0; i < level->model.mesh_count; ++i)
    {
        SDL_free(level->model.meshes[i].lightmap_uvs);
        level->model.meshes[i].lightmap_uvs = NULL;
    }
}

bool level_data_init(level_data *ld)
{
    int mc = (int)SDL_arraysize(g_mats);
    ld->use_lit = true;
    ld->use_lightmaps = true;

    if (!sdl3d_build_level(g_sectors, g_sector_count, g_mats, mc, g_lights, g_light_count, &ld->lightmapped))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build (lightmapped) failed: %s", SDL_GetError());
        return false;
    }
    if (!sdl3d_build_level(g_sectors, g_sector_count, g_mats, mc, g_lights, g_light_count, &ld->vertex_baked))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build (vertex-baked) failed: %s", SDL_GetError());
        sdl3d_free_level(&ld->lightmapped);
        return false;
    }
    strip_lightmap(&ld->vertex_baked);
    if (!sdl3d_build_level(g_sectors, g_sector_count, g_mats, mc, NULL, 0, &ld->unlit))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level build (unlit) failed: %s", SDL_GetError());
        sdl3d_free_level(&ld->lightmapped);
        sdl3d_free_level(&ld->vertex_baked);
        return false;
    }
    return true;
}

void level_data_free(level_data *ld)
{
    sdl3d_free_level(&ld->lightmapped);
    sdl3d_free_level(&ld->vertex_baked);
    sdl3d_free_level(&ld->unlit);
}

sdl3d_level *level_data_active(level_data *ld)
{
    if (!ld->use_lit)
        return &ld->unlit;
    return ld->use_lightmaps ? &ld->lightmapped : &ld->vertex_baked;
}

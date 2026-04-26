/* Level content: sector geometry, materials, lights, and built levels. */
#ifndef DOOM_LEVEL_DATA_H
#define DOOM_LEVEL_DATA_H

#include "sdl3d/level.h"

#include <stdbool.h>

typedef struct level_data
{
    sdl3d_level lightmapped;
    sdl3d_level vertex_baked;
    sdl3d_level unlit;
    bool use_lit;
    bool use_lightmaps;
} level_data;

#define DOOM_DYNAMIC_LIFT_SECTOR 32
#define DOOM_NUKAGE_BASIN_SECTOR 2
#define DOOM_AMBIENT_DEMO_SECTOR 33
#define DOOM_AMBIENT_DEMO_SOUND_ID 1
#define DOOM_CONVEYOR_SECTOR 35

/* Sector and light arrays are exposed so the player/renderer can query them. */
extern sdl3d_sector g_sectors[];
extern const int g_sector_count;
extern const sdl3d_level_light g_lights[];
extern const int g_light_count;

bool level_data_init(level_data *ld);
void level_data_free(level_data *ld);
bool level_data_set_sector_geometry(level_data *ld, int sector_index, const sdl3d_sector_geometry *geometry);

/* Return the active level variant based on current toggle state. */
sdl3d_level *level_data_active(level_data *ld);

#endif

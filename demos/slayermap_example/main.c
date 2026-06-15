#include <slayer3d/map.h>

#include <stdio.h>

#ifndef SLAYER3D_SLAYERMAP_EXAMPLE_MAP
#define SLAYER3D_SLAYERMAP_EXAMPLE_MAP "maps/training_room.slayermap.json"
#endif

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : SLAYER3D_SLAYERMAP_EXAMPLE_MAP;
    char error[512] = {0};
    slayer3d_map_document *map = NULL;

    if (!slayer3d_map_load_file(path, NULL, &map, error, (int)sizeof(error)))
    {
        fprintf(stderr, "failed to load Slayer3D map '%s': %s\n", path, error[0] != '\0' ? error : "unknown error");
        return 1;
    }

    printf("Loaded %s\n", slayer3d_map_get_metadata_name(map));
    printf("  source: %s\n", slayer3d_map_get_source_path(map));
    printf("  materials: %zu\n", slayer3d_map_get_material_count(map));
    printf("  brushes: %zu\n", slayer3d_map_get_brush_count(map));
    printf("  actors: %zu\n", slayer3d_map_get_actor_count(map));
    printf("  prefabs: %zu\n", slayer3d_map_get_prefab_count(map));
    printf("  lights: %zu\n", slayer3d_map_get_light_count(map));
    printf("  effects: %zu\n", slayer3d_map_get_effect_count(map));
    printf("  skybox: %s\n", slayer3d_map_has_skybox(map) ? "yes" : "no");
    printf("  connections: %zu\n", slayer3d_map_get_connection_count(map));

    slayer3d_map_destroy(map);
    return 0;
}

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

    slayer3d_map_asset texture;
    if (slayer3d_map_get_asset(map, SLAYER3D_MAP_ASSET_TEXTURE, 0, &texture))
        printf("  first texture asset: %s path=%s\n", texture.id, texture.path);

    slayer3d_map_material material;
    if (slayer3d_map_get_material(map, 0, &material))
        printf("  first material: %s texture=%s\n", material.id, material.texture != NULL ? material.texture : "none");

    slayer3d_map_actor actor;
    if (slayer3d_map_get_actor(map, 0, &actor))
    {
        printf("  first actor: %s archetype=%s primitive=%s\n", actor.id,
               actor.archetype != NULL ? actor.archetype : "none", actor.primitive != NULL ? actor.primitive : "none");
        if (actor.transform.has_position)
        {
            printf("    position: %.2f %.2f %.2f\n", actor.transform.position.x, actor.transform.position.y,
                   actor.transform.position.z);
        }
    }

    slayer3d_map_playable_scene_desc scene;
    if (slayer3d_map_build_playable_scene_desc(map, &scene, error, (int)sizeof(error)))
    {
        printf("  playable scene: %zu brushes (%zu box), %zu actors\n", scene.playable_brush_count,
               scene.box_brush_count, scene.actor_count);
        printf("    player: %s at %.2f %.2f %.2f\n", scene.player_actor_id, scene.player_position.x,
               scene.player_position.y, scene.player_position.z);
    }
    else
    {
        printf("  playable scene: unavailable (%s)\n", error[0] != '\0' ? error : "missing player-character");
    }

    slayer3d_map_destroy(map);
    return 0;
}

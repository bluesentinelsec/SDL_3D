/**
 * @file game_data_world.h
 * @brief Public world-model descriptors for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_WORLD_H
#define SLAYER3D_GAME_DATA_WORLD_H

#include <stdbool.h>

#include "slayer3d/level.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Runtime descriptor for a JSON-authored sector level.
     *
     * Sector levels are data-authored Doom/Quake-style indoor worlds. The
     * runtime owns all pointers in this descriptor. They remain valid until
     * slayer3d_game_data_destroy().
     */
    typedef struct slayer3d_game_data_sector_level
    {
        /** @brief Stable authored level name. */
        const char *name;
        /** @brief Runtime sector definitions used for collision, sensors, and future mutation. */
        const slayer3d_sector *sectors;
        /** @brief Optional authored sector names parallel to @p sectors. */
        const char *const *sector_names;
        /** @brief Number of entries in @p sectors and @p sector_names. */
        int sector_count;
        /** @brief Runtime material palette used to build the sector meshes. */
        const slayer3d_level_material *materials;
        /** @brief Number of entries in @p materials. */
        int material_count;
        /** @brief Authored baked-light definitions. */
        const slayer3d_level_light *lights;
        /** @brief Number of entries in @p lights. */
        int light_count;
        /** @brief Level built with authored baked lights and lightmap atlas data. */
        const slayer3d_level *lightmapped;
        /** @brief Level built with baked vertex colors but no lightmap atlas data. */
        const slayer3d_level *vertex_baked;
        /** @brief Level built without baked lights. */
        const slayer3d_level *unlit;
    } slayer3d_game_data_sector_level;

    /** @brief Runtime mesh variant selected for an authored sector level instance. */
    typedef enum slayer3d_game_data_sector_level_variant
    {
        /** @brief Baked lightmap atlas variant. */
        SLAYER3D_GAME_DATA_SECTOR_LEVEL_LIGHTMAPPED = 1,
        /** @brief Baked per-vertex lighting variant without a lightmap atlas. */
        SLAYER3D_GAME_DATA_SECTOR_LEVEL_VERTEX_BAKED = 2,
        /** @brief Unlit material variant. */
        SLAYER3D_GAME_DATA_SECTOR_LEVEL_UNLIT = 3,
    } slayer3d_game_data_sector_level_variant;

    /**
     * @brief Active-scene instance of an authored sector level.
     *
     * Scene files declare sector level instances under `world.sector_levels`.
     * The runtime resolves the authored level name to built level data and
     * supplies the selected render variant. Pointers are runtime-owned.
     */
    typedef struct slayer3d_game_data_sector_level_instance
    {
        /** @brief Authored sector level name. */
        const char *level_name;
        /** @brief Authored variant name, such as `lightmapped`. */
        const char *variant_name;
        /** @brief Selected built level variant. */
        slayer3d_game_data_sector_level_variant variant;
        /** @brief Built level selected by @p variant. */
        const slayer3d_level *level;
        /** @brief Runtime sector definitions parallel to @p level. */
        const slayer3d_sector *sectors;
        /** @brief Number of entries in @p sectors. */
        int sector_count;
        /** @brief World-space translation applied before drawing. */
        slayer3d_vec3 position;
        /** @brief Whether renderers should compute portal visibility for this instance. */
        bool portal_culling;
        /** @brief Whether authored sector-local lighting is applied to this instance. */
        bool sector_lighting_enabled;
    } slayer3d_game_data_sector_level_instance;

    /** @brief Runtime-owned node resolved from an authored sector navigation graph. */
    typedef struct slayer3d_game_data_sector_nav_node
    {
        /** @brief Authored node name. */
        const char *name;
        /** @brief Sector index in the graph's sector level, or -1 when unresolved. */
        int sector_index;
        /** @brief World-space navigation anchor position. */
        slayer3d_vec3 position;
    } slayer3d_game_data_sector_nav_node;

    /**
     * @brief Callback for active authored sector level instances.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_sector_level_instance_fn)(
        void *userdata, const slayer3d_game_data_sector_level_instance *instance);

#ifdef __cplusplus
}
#endif

#endif

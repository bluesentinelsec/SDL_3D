/**
 * @file game_data_editor_metadata.h
 * @brief Shared editor/tooling metadata for JSON-authored game data objects.
 */

#ifndef SLAYER3D_GAME_DATA_EDITOR_METADATA_H
#define SLAYER3D_GAME_DATA_EDITOR_METADATA_H

#include <stdbool.h>

#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Optional editor/tooling metadata attached to authored objects. */
    typedef struct slayer3d_game_data_editor_metadata
    {
        /** @brief Optional stable tooling id that survives display-name changes. */
        const char *stable_id;
        /** @brief Human-readable name for palettes and inspectors. */
        const char *display_name;
        /** @brief Human-readable description for tooling. */
        const char *description;
        /** @brief Tooling category path, such as `brushes/architecture`. */
        const char *category;
        /** @brief Optional grouping label for hierarchy views. */
        const char *group;
        /** @brief Optional prefab/template reference. */
        const char *prefab;
        /** @brief Optional actor archetype reference for placement tools. */
        const char *archetype;
        /** @brief Optional icon asset or symbolic icon id. */
        const char *icon;
        /** @brief Optional preview asset id/path. */
        const char *preview_asset;
        /** @brief Optional authored tags for filtering. */
        const char *const *tags;
        /** @brief Number of entries in @p tags. */
        int tag_count;
        /** @brief True when an explicit snap grid was authored. */
        bool has_snap_grid;
        /** @brief Tooling snap grid in authored units. */
        slayer3d_vec3 snap_grid;
        /** @brief Positive snap rotation in degrees, or 0 when omitted. */
        float snap_rotation_degrees;
        /** @brief Whether placement tools should align the object to floors. */
        bool snap_align_to_floor;
    } slayer3d_game_data_editor_metadata;

#ifdef __cplusplus
}
#endif

#endif

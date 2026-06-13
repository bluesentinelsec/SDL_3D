/**
 * @file game_data_world_model.h
 * @brief Public world-model query descriptors for JSON-authored games.
 */

#ifndef SLAYER3D_GAME_DATA_WORLD_MODEL_H
#define SLAYER3D_GAME_DATA_WORLD_MODEL_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_world.h"
#include "slayer3d/level.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Runtime world model implementation kind. */
    typedef enum slayer3d_game_data_world_model_type
    {
        /** @brief Invalid or unavailable world model type. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID = 0,
        /** @brief Sector/portal world model built from 2.5D sector geometry. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL = 1,
        /** @brief Convex brush world model built from true 3D brush planes. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD = 2,
        /** @brief Editor-authored player-start marker selected by tooling. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START = 3,
        /** @brief Editor-authored actor placement marker selected by tooling. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR = 4,
    } slayer3d_game_data_world_model_type;

    enum
    {
        /** @brief Include authored sector level instances in generic world-model queries. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS = 1u << 0,
        /** @brief Include authored brush world instances in generic world-model queries. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS = 1u << 1,
        /** @brief Include every supported authored world model in generic world-model queries. */
        SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL =
            SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS | SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS,
    };

    /** @brief Active-scene world model instance descriptor for editor/tooling code. */
    typedef struct slayer3d_game_data_world_model_instance
    {
        /** @brief Runtime implementation kind. */
        slayer3d_game_data_world_model_type type;
        /** @brief Authored world model name, such as a sector level or brush world id. */
        const char *name;
        /** @brief Optional authored variant/debug label, such as a sector level variant. */
        const char *variant_name;
        /** @brief World-space translation applied to this model instance. */
        slayer3d_vec3 position;
        /** @brief World-space bounds for the instance when known. */
        slayer3d_bounding_box bounds;
        /** @brief True when @p bounds contains usable world-space bounds. */
        bool has_bounds;
        /** @brief Sector level for sector instances, otherwise NULL. */
        const slayer3d_level *sector_level;
        /** @brief Runtime sectors for sector instances, otherwise NULL. */
        const slayer3d_sector *sectors;
        /** @brief Number of entries in @p sectors. */
        int sector_count;
        /** @brief Brush world for brush instances, otherwise NULL. */
        const slayer3d_game_data_brush_world *brush_world;
    } slayer3d_game_data_world_model_instance;

    /** @brief Generic trace descriptor for active world model queries. */
    typedef struct slayer3d_game_data_world_trace_desc
    {
        /** @brief Trace start point in world coordinates. */
        slayer3d_vec3 start;
        /** @brief Trace end point in world coordinates. */
        slayer3d_vec3 end;
        /** @brief Shape to sweep. Sector levels currently support point traces; brush worlds support all values. */
        slayer3d_game_data_brush_trace_shape shape;
        /** @brief Sphere radius in x, or AABB half-extents for xyz. */
        slayer3d_vec3 extents;
        /** @brief Bitmask of SLAYER3D_GAME_DATA_BRUSH_CONTENT_* flags for brush traces. */
        unsigned int contents_mask;
        /** @brief Bitmask of SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_* flags, or 0 for all models. */
        unsigned int model_filter;
    } slayer3d_game_data_world_trace_desc;

    /** @brief Generic trace/pick result for active world model queries. */
    typedef struct slayer3d_game_data_world_trace_result
    {
        /** @brief True when the trace intersected or exited a matching world model. */
        bool hit;
        /** @brief True when the trace starts inside a matching solid brush. */
        bool start_solid;
        /** @brief True when the trace remains inside a matching solid brush. */
        bool all_solid;
        /** @brief Implementation that produced the hit. */
        slayer3d_game_data_world_model_type type;
        /** @brief Authored world model name. */
        const char *world_name;
        /** @brief World-space translation of the hit world-model instance. */
        slayer3d_vec3 world_position;
        /** @brief Authored sector/brush name when available. */
        const char *element_name;
        /** @brief Authored material name for brush face hits, or NULL. */
        const char *material_name;
        /** @brief Sector or brush index, or -1 when unavailable. */
        int element_index;
        /** @brief Brush face index, or -1 when unavailable. */
        int face_index;
        /** @brief First hit fraction in [0, 1] along start-to-end. */
        float fraction;
        /** @brief End position at @p fraction. */
        slayer3d_vec3 end_position;
        /** @brief Hit point on the swept shape origin path. */
        slayer3d_vec3 point;
        /** @brief Hit plane normal for brush traces, or zero for sector exit traces. */
        slayer3d_vec3 normal;
        /** @brief Brush contents bitmask, or 0 for sector traces. */
        unsigned int contents;
        /** @brief Brush surface flags, or 0 for sector traces. */
        unsigned int surface_flags;
    } slayer3d_game_data_world_trace_result;

    /** @brief Generic point-contents result for active world model queries. */
    typedef struct slayer3d_game_data_world_point_result
    {
        /** @brief True when the point lies inside a matching world model volume. */
        bool inside;
        /** @brief Implementation that contains the point. */
        slayer3d_game_data_world_model_type type;
        /** @brief Authored world model name. */
        const char *world_name;
        /** @brief World-space translation of the selected world-model instance. */
        slayer3d_vec3 world_position;
        /** @brief Authored sector/brush name when available. */
        const char *element_name;
        /** @brief Sector or brush index, or -1 when unavailable. */
        int element_index;
        /** @brief Brush contents bitmask, or 0 for sector point queries. */
        unsigned int contents;
    } slayer3d_game_data_world_point_result;

    /** @brief Generic world-model diagnostics for editor/debug UI. */
    typedef struct slayer3d_game_data_world_model_diagnostics
    {
        /** @brief Active sector level instances enumerated by the last diagnostic query. */
        Uint64 active_sector_level_instances;
        /** @brief Active brush world instances enumerated by the last diagnostic query. */
        Uint64 active_brush_world_instances;
        /** @brief Cumulative generic world trace requests. */
        Uint64 world_trace_count;
        /** @brief Cumulative generic point-contents requests. */
        Uint64 point_query_count;
        /** @brief Existing brush-world trace/render diagnostics. */
        slayer3d_game_data_brush_diagnostics brush;
    } slayer3d_game_data_world_model_diagnostics;

#ifdef __cplusplus
}
#endif

#endif

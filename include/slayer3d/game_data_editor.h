/**
 * @file game_data_editor.h
 * @brief Public editor/tooling data descriptors for JSON-authored games.
 */

#ifndef SLAYER3D_GAME_DATA_EDITOR_H
#define SLAYER3D_GAME_DATA_EDITOR_H

#include <stdbool.h>

#include "slayer3d/game_data_editor_metadata.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Editor/tooling selection produced by world picking or an authored work plane. */
    typedef struct slayer3d_game_data_editor_selection
    {
        /** @brief True when the selection hit a world model or work plane. */
        bool hit;
        /** @brief Implementation kind that produced the selection, or INVALID for a work-plane hit. */
        slayer3d_game_data_world_model_type type;
        /** @brief Authored world model name. */
        const char *world_name;
        /** @brief World-space translation of the selected world-model instance. */
        slayer3d_vec3 world_position;
        /** @brief Authored sector/brush name when available. */
        const char *element_name;
        /** @brief Authored brush material name for face selections, or NULL. */
        const char *material_name;
        /** @brief Sector or brush index, or -1 when unavailable. */
        int element_index;
        /** @brief Brush face index, or -1 when unavailable. */
        int face_index;
        /** @brief Trace fraction in [0, 1]. */
        float fraction;
        /** @brief World-space hit point. */
        slayer3d_vec3 point;
        /** @brief World-space hit normal when available. */
        slayer3d_vec3 normal;
        /** @brief World-space selection bounds when known. */
        slayer3d_bounding_box bounds;
        /** @brief True when @p bounds contains usable world-space bounds. */
        bool has_bounds;
        /** @brief Optional world-level editor metadata. */
        const slayer3d_game_data_editor_metadata *world_editor;
        /** @brief Optional sector/brush editor metadata. */
        const slayer3d_game_data_editor_metadata *element_editor;
        /** @brief Optional material editor metadata for brush face hits. */
        const slayer3d_game_data_editor_metadata *material_editor;
        /** @brief Optional face editor metadata for brush face hits. */
        const slayer3d_game_data_editor_metadata *face_editor;
        /** @brief Visible compiled render face backing this source face, or NULL when culled/not applicable. */
        const slayer3d_game_data_brush_compiled_face *compiled_face;
        /** @brief Index into the owning brush world's compile_rendered_faces table, or -1 when unavailable. */
        int compiled_face_index;
    } slayer3d_game_data_editor_selection;

    /** @brief Editor debug primitive kind emitted by tooling helpers. */
    typedef enum slayer3d_game_data_editor_debug_primitive_type
    {
        /** @brief Active world-model bounds edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORLD_BOUNDS_EDGE = 1,
        /** @brief Selected object bounds edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_BOUNDS_EDGE = 2,
        /** @brief Selection trace ray. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_TRACE_RAY = 3,
        /** @brief Selected face normal line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_FACE_NORMAL = 4,
        /** @brief Hit-point marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_HIT_MARKER = 5,
        /** @brief Non-mutating editor command preview bounds edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_COMMAND_PREVIEW_BOUNDS_EDGE = 6,
        /** @brief Editor grid line clipped to a visible brush surface. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORK_PLANE_GRID = 7,
        /** @brief Editor-authored player-start marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLAYER_START_EDGE = 8,
        /** @brief Data-authored diagnostic marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DIAGNOSTIC_MARKER = 9,
        /** @brief Selected brush source face edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SELECTION_FACE_EDGE = 10,
        /** @brief Selected source brush vertex handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HANDLE = 11,
        /** @brief Active source vertex drag guide line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_DRAG_GUIDE = 12,
        /** @brief Pending add-vertex preview handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_ADD_PREVIEW = 13,
        /** @brief Selected source brush vertex handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_SELECTED_HANDLE = 14,
        /** @brief Hovered source brush vertex handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_HANDLE = 15,
        /** @brief Hovered source brush vertex coordinate label. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_VERTEX_HOVER_LABEL = 16,
        /** @brief Pending brush placement footprint edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLACEMENT_PREVIEW_FOOTPRINT_EDGE = 17,
        /** @brief Pending brush placement extrusion-axis guide. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLACEMENT_PREVIEW_AXIS = 18,
        /** @brief Source clip preview edge for geometry that will be kept. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PREVIEW_KEPT_EDGE = 19,
        /** @brief Source clip preview edge for geometry that will be discarded. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PREVIEW_DISCARDED_EDGE = 20,
        /** @brief Clip tool point handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_POINT_HANDLE = 21,
        /** @brief Hovered clip tool point handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_POINT_HOVER_HANDLE = 22,
        /** @brief Clip tool point-to-point line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_LINE = 23,
        /** @brief Clip tool plane indicator edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_PLANE_EDGE = 24,
        /** @brief Clip tool status label. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_STATUS_LABEL = 25,
        /** @brief Clip tool source-geometry snap target marker. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_CLIP_SNAP_TARGET = 26,
        /** @brief Editable source brush edge line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_EDITABLE_EDGE = 27,
        /** @brief Editable source brush edge midpoint handle. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_HANDLE = 28,
        /** @brief Hovered source brush edge midpoint handle. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_HOVER_HANDLE = 29,
        /** @brief Selected source brush edge midpoint handle. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_EDGE_SELECTED_HANDLE = 30,
        /** @brief Rotate tool pivot marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_PIVOT = 31,
        /** @brief Rotate tool axis ring line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_RING = 32,
        /** @brief Rotate tool active drag angle arc line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_ARC = 33,
        /** @brief Rotate tool non-mutating transformed source preview edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ROTATE_PREVIEW_EDGE = 34,
        /** @brief Scale tool bounds-handle marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SCALE_HANDLE = 35,
        /** @brief Scale tool hovered or active bounds-handle marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SCALE_HOVER_HANDLE = 36,
        /** @brief Scale tool non-mutating transformed source preview edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SCALE_PREVIEW_EDGE = 37,
        /** @brief Shear tool bounds-side handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SHEAR_HANDLE = 38,
        /** @brief Shear tool hovered or active bounds-side handle line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SHEAR_HOVER_HANDLE = 39,
        /** @brief Shear tool non-mutating transformed source preview edge. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_SHEAR_PREVIEW_EDGE = 40,
        /** @brief Dedicated editor origin axis line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ORIGIN_AXIS = 41,
        /** @brief Editor-authored actor placement marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_ACTOR_EDGE = 42,
    } slayer3d_game_data_editor_debug_primitive_type;

    enum
    {
        /** @brief Emit active world-model bounds. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS = 1u << 0,
        /** @brief Emit selected element bounds when known. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS = 1u << 1,
        /** @brief Emit the trace ray stored in the debug descriptor. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY = 1u << 2,
        /** @brief Emit selected face normal when known. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL = 1u << 3,
        /** @brief Emit a small marker at the selected hit point. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER = 1u << 4,
        /** @brief Emit active non-mutating editor command preview bounds. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW = 1u << 5,
        /** @brief Emit editor brush-surface grid lines and origin axes. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID = 1u << 6,
        /** @brief Emit editor-authored player-start marker icons. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS = 1u << 7,
        /** @brief Emit data-authored diagnostic markers. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_DIAGNOSTIC_MARKERS = 1u << 8,
        /** @brief Emit the selected source brush face outline when known. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_FACE = 1u << 9,
        /** @brief Emit source vertex handles for selected brushes. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES = 1u << 10,
        /** @brief Emit active source clip preview edges. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_CLIP_PREVIEW = 1u << 11,
        /** @brief Emit source edge handles for selected brushes. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_EDGE_HANDLES = 1u << 12,
        /** @brief Emit rotate tool pivot and axis handles. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ROTATE_HANDLES = 1u << 13,
        /** @brief Emit scale tool bounds handles. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SCALE_HANDLES = 1u << 14,
        /** @brief Emit shear tool bounds side handles. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SHEAR_HANDLES = 1u << 15,
        /** @brief Emit editor-authored actor placement markers. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ACTORS = 1u << 16,
        /** @brief Emit every supported editor debug primitive. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL =
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_DIAGNOSTIC_MARKERS |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_FACE | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_VERTEX_HANDLES |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_CLIP_PREVIEW | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_EDGE_HANDLES |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ROTATE_HANDLES | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SCALE_HANDLES |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SHEAR_HANDLES | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ACTORS,
    };

    /** @brief One renderer-agnostic editor debug line segment. */
    typedef struct slayer3d_game_data_editor_debug_primitive
    {
        /** @brief Semantic line type. */
        slayer3d_game_data_editor_debug_primitive_type type;
        /** @brief World-space line start. */
        slayer3d_vec3 start;
        /** @brief World-space line end. */
        slayer3d_vec3 end;
        /** @brief Display color. */
        slayer3d_color color;
        /** @brief Associated world name when available. */
        const char *world_name;
        /** @brief Associated sector/brush name when available. */
        const char *element_name;
        /** @brief Associated brush face index, or -1. */
        int face_index;
        /** @brief Optional overlay label text for non-line debug primitives. */
        char text[64];
    } slayer3d_game_data_editor_debug_primitive;

    /** @brief Editor debug overlay generation options. */
    typedef struct slayer3d_game_data_editor_debug_desc
    {
        /** @brief Bitmask of SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_* flags. */
        unsigned int flags;
        /** @brief Optional current selection. */
        const slayer3d_game_data_editor_selection *selection;
        /** @brief Optional trace descriptor used for trace-ray visualization. */
        const slayer3d_game_data_world_trace_desc *trace;
        /** @brief Color for active world-model bounds, or alpha 0 for default. */
        slayer3d_color world_bounds_color;
        /** @brief Color for selected element bounds, or alpha 0 for default. */
        slayer3d_color selection_bounds_color;
        /** @brief Color for selected source face outline, or alpha 0 for default. */
        slayer3d_color selection_face_color;
        /** @brief Color for trace rays, or alpha 0 for default. */
        slayer3d_color trace_color;
        /** @brief Color for face normals, or alpha 0 for default. */
        slayer3d_color face_normal_color;
        /** @brief Color for hit markers, or alpha 0 for default. */
        slayer3d_color hit_marker_color;
        /** @brief Color for command preview bounds, or alpha 0 for default. */
        slayer3d_color command_preview_color;
        /** @brief Color for brush-surface grid lines, or alpha 0 for default. */
        slayer3d_color work_plane_grid_color;
        /** @brief Color for editor player-start marker lines, or alpha 0 for default. */
        slayer3d_color player_start_color;
        /** @brief Color for selected source vertex handles, or alpha 0 for default. */
        slayer3d_color vertex_handle_color;
        /** @brief True when work-plane grid settings are valid. */
        bool has_work_plane_grid;
        /** @brief Work-plane normal. */
        slayer3d_vec3 work_plane_normal;
        /** @brief Work-plane distance for dot(normal, point) = distance. */
        float work_plane_distance;
        /** @brief Half-extent of the grid in world units. Defaults to 16. */
        float work_plane_grid_size;
        /** @brief Grid line spacing in world units. Defaults to 1. */
        float work_plane_grid_spacing;
        /** @brief Face-normal line length in world units. Defaults to 0.75. */
        float normal_length;
        /** @brief Hit-marker half-size in world units. Defaults to 0.1. */
        float hit_marker_size;
        /** @brief Player-start marker radius in world units. Defaults to 0.35. */
        float player_start_radius;
        /** @brief Player-start marker height in world units. Defaults to 1.8. */
        float player_start_height;
    } slayer3d_game_data_editor_debug_desc;

    /** @brief Callback for renderer-agnostic editor debug primitive iteration. */
    typedef bool (*slayer3d_game_data_editor_debug_primitive_fn)(
        void *userdata, const slayer3d_game_data_editor_debug_primitive *primitive);

#ifdef __cplusplus
}
#endif

#endif

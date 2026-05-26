/**
 * @file game_data.h
 * @brief JSON-authored game data runtime.
 *
 * The game data runtime loads an SLAYER3D game JSON file and instantiates its
 * generic composition primitives into an existing game session: actors, input
 * actions, signals, timers, sensors, and signal-to-action bindings.
 *
 * Game-specific behavior stays behind named adapters. JSON chooses where an
 * adapter is invoked and can bind it to a Lua function loaded from a script
 * next to the data file. Game code may also register native C callbacks for
 * adapters that need host integration or optimized native code.
 */

#ifndef SLAYER3D_GAME_DATA_H
#define SLAYER3D_GAME_DATA_H

#include <stdbool.h>

#include "slayer3d/actor_registry.h"
#include "slayer3d/asset.h"
#include "slayer3d/camera.h"
#include "slayer3d/effects.h"
#include "slayer3d/font.h"
#include "slayer3d/game.h"
#include "slayer3d/game_data_app.h"
#include "slayer3d/game_data_assets.h"
#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_defaults.h"
#include "slayer3d/game_data_editor.h"
#include "slayer3d/game_data_editor_metadata.h"
#include "slayer3d/game_data_network.h"
#include "slayer3d/game_data_render.h"
#include "slayer3d/game_data_scene.h"
#include "slayer3d/game_data_ui.h"
#include "slayer3d/game_data_world.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/level.h"
#include "slayer3d/lighting.h"
#include "slayer3d/model.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"
#include "slayer3d/render_context.h"
#include "slayer3d/sprite_asset.h"
#include "slayer3d/storage.h"
#include "slayer3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Callback for active authored brush world instances.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_brush_world_instance_fn)(void *userdata,
                                                               const slayer3d_game_data_brush_world_instance *instance);

    /**
     * @brief Callback for active authored world model instances.
     *
     * Return false to stop iteration early. The descriptor and all nested
     * pointers are valid only for the duration of the callback; strings and
     * native world pointers are runtime-owned.
     */
    typedef bool (*slayer3d_game_data_world_model_instance_fn)(void *userdata,
                                                               const slayer3d_game_data_world_model_instance *instance);

    /**
     * @brief Persistent state bag shared across authored scene changes.
     *
     * Scene-transition payloads are transient and exist only while the target
     * scene's enter signal is emitted. This runtime-owned bag is the durable
     * handoff point for data that should survive after the transition, such as
     * selected character, level index, difficulty, or inventory snapshot ids.
     *
     * The returned pointer is owned by @p runtime and remains valid until the
     * runtime is destroyed. Callers may mutate it with the normal
     * slayer3d_properties setters.
     */
    slayer3d_properties *slayer3d_game_data_mutable_scene_state(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the persistent scene-state bag.
     *
     * @see slayer3d_game_data_mutable_scene_state
     */
    const slayer3d_properties *slayer3d_game_data_scene_state(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Look up a JSON-authored sector level by name.
     *
     * This exposes loaded and built sector-world data to renderer,
     * controller, sensor, and editor systems without making callers parse
     * JSON. The returned pointers are runtime-owned.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored sector level name.
     * @param out_level Receives runtime-owned sector level pointers.
     * @return true when @p name resolves to an authored sector level.
     */
    bool slayer3d_game_data_get_sector_level(const slayer3d_game_data_runtime *runtime, const char *name,
                                             slayer3d_game_data_sector_level *out_level);

    /**
     * @brief Look up a JSON-authored brush world by name.
     *
     * This exposes loaded native brush-world data to renderer, collision,
     * controller, sensor, and editor systems without making callers parse JSON.
     * The returned pointers are runtime-owned.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored brush world name.
     * @param out_world Receives runtime-owned brush world pointers.
     * @return true when @p name resolves to an authored brush world.
     */
    bool slayer3d_game_data_get_brush_world(const slayer3d_game_data_runtime *runtime, const char *name,
                                            slayer3d_game_data_brush_world *out_world);

    /**
     * @brief Trace a point, sphere, or AABB through one named brush world.
     *
     * Coordinates are local to the named brush world. Use
     * slayer3d_game_data_trace_active_brush_worlds() for active-scene
     * world-space queries that honor `world.brush_worlds` placement.
     *
     * The function tests only brushes whose `contents` overlap
     * `desc.contents_mask`. It returns true for a valid query regardless of
     * whether a hit occurred; inspect `out_result.hit`.
     */
    bool slayer3d_game_data_trace_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                              const slayer3d_game_data_brush_trace_desc *desc,
                                              slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Trace a point, sphere, or AABB through brush worlds in the active scene.
     *
     * Input and output positions are world-space. The closest hit across all
     * active-scene brush world instances is returned.
     */
    bool slayer3d_game_data_trace_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_brush_trace_desc *desc,
                                                      slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Move through one named brush world while sliding along hit planes.
     *
     * This is a thin deterministic helper over brush traces. It repeatedly
     * traces the requested movement, projects the remaining motion along the
     * blocking plane, and returns the final non-penetrating end position in
     * `out_result.end_position`. The first blocking hit metadata is preserved.
     */
    bool slayer3d_game_data_slide_brush_world(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                              const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                              slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Active-scene world-space variant of slayer3d_game_data_slide_brush_world().
     */
    bool slayer3d_game_data_slide_active_brush_worlds(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_brush_trace_desc *desc, int max_bumps,
                                                      slayer3d_game_data_brush_trace_result *out_result);

    /**
     * @brief Copy accumulated brush-world trace diagnostics.
     *
     * Diagnostics are cumulative until reset and are intended for tests,
     * debug UI, and future editor instrumentation.
     */
    bool slayer3d_game_data_get_brush_diagnostics(const slayer3d_game_data_runtime *runtime,
                                                  slayer3d_game_data_brush_diagnostics *out_diagnostics);

    /** @brief Reset accumulated brush-world trace diagnostics to zero. */
    void slayer3d_game_data_reset_brush_diagnostics(slayer3d_game_data_runtime *runtime);

    /** @brief Add render-context stat deltas to accumulated brush diagnostics. */
    void slayer3d_game_data_accumulate_brush_render_diagnostics(slayer3d_game_data_runtime *runtime,
                                                                const slayer3d_render_stats *before,
                                                                const slayer3d_render_stats *after);

    /**
     * @brief Find the nearest authored navigation node in a sector navigation graph.
     *
     * If @p position is inside the graph's sector level, nodes in that sector
     * are preferred. If no same-sector node exists, the nearest node in the
     * graph is returned. The returned node name is runtime-owned.
     */
    bool slayer3d_game_data_sector_nav_nearest_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                    slayer3d_vec3 position,
                                                    slayer3d_game_data_sector_nav_node *out_node);

    /**
     * @brief Resolve an authored sector navigation path between two world positions.
     *
     * The start and goal positions are first anchored to nearest nodes in the
     * graph, then Dijkstra search is run across authored links. @p out_nodes
     * may be NULL when the caller only needs @p out_node_count or @p out_cost.
     * When @p out_nodes is non-NULL, @p max_nodes must be large enough for the
     * full path or the function returns false.
     */
    bool slayer3d_game_data_sector_nav_path(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                            slayer3d_vec3 start, slayer3d_vec3 goal,
                                            slayer3d_game_data_sector_nav_node *out_nodes, int max_nodes,
                                            int *out_node_count, float *out_cost);

    /**
     * @brief Return whether an authored sector navigation path exists between two positions.
     */
    bool slayer3d_game_data_sector_nav_path_available(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                      slayer3d_vec3 start, slayer3d_vec3 goal);

    /**
     * @brief Resolve the next node after the start anchor on a sector navigation path.
     *
     * If the start and goal anchor to the same node, that node is returned.
     */
    bool slayer3d_game_data_sector_nav_next_node(const slayer3d_game_data_runtime *runtime, const char *graph_name,
                                                 slayer3d_vec3 start, slayer3d_vec3 goal,
                                                 slayer3d_game_data_sector_nav_node *out_node);

    /**
     * @brief Read authored or runtime-edited sector-local lighting.
     *
     * @p sector may be either a sector name or a decimal sector index. Returns
     * false when the level/sector cannot be resolved, when the sector has no
     * authored lighting, or when the output pointers are invalid. @p out_color
     * receives RGB tint plus alpha/influence in [0, 1].
     */
    bool slayer3d_game_data_get_sector_lighting(const slayer3d_game_data_runtime *runtime, const char *sector_level,
                                                const char *sector, float *out_level, float out_color[4],
                                                char *error_buffer, int error_buffer_size);

    /**
     * @brief Set sector-local lighting and rebuild the level's render variants.
     *
     * @p sector may be either a sector name or a decimal sector index. @p level
     * is clamped to [0, 255]. @p color is clamped per channel to [0, 1] and
     * uses color[3] as tint influence, not transparency. The update is atomic:
     * if rebuilding fails, the previous runtime level data remains active.
     */
    bool slayer3d_game_data_set_sector_lighting(slayer3d_game_data_runtime *runtime, const char *sector_level,
                                                const char *sector, float level, const float color[4],
                                                char *error_buffer, int error_buffer_size);

    /**
     * @brief Iterate sector levels declared by the active scene.
     *
     * The active scene owns placement and variant selection through
     * `world.sector_levels`. This helper is renderer-agnostic so tests,
     * editors, and custom hosts can inspect the same resolved instances that
     * the generic presentation layer draws.
     */
    bool slayer3d_game_data_for_each_sector_level_instance(const slayer3d_game_data_runtime *runtime,
                                                           slayer3d_game_data_sector_level_instance_fn callback,
                                                           void *userdata);

    /**
     * @brief Iterate brush worlds declared by the active scene.
     *
     * The active scene owns placement and debug policy through
     * `world.brush_worlds`. This helper is renderer-agnostic so tests,
     * editors, and future brush render/collision systems can inspect the same
     * resolved instances.
     */
    bool slayer3d_game_data_for_each_brush_world_instance(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_game_data_brush_world_instance_fn callback,
                                                          void *userdata);

    /**
     * @brief Iterate all active scene world model instances through a common descriptor.
     *
     * This is the editor/tooling-facing enumeration layer over sector and
     * brush worlds. It allows tools to inspect placement, bounds, and stable
     * world references without branching over the authored scene JSON shape.
     */
    bool slayer3d_game_data_for_each_world_model_instance(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_game_data_world_model_instance_fn callback,
                                                          void *userdata);

    /**
     * @brief Trace through active scene world model instances.
     *
     * Brush worlds use the existing brush trace implementation. Sector levels
     * currently support point traces that detect exiting sector volume; shaped
     * traces are ignored for sector models and still evaluated against brush
     * models when included by @p desc.model_filter.
     */
    bool slayer3d_game_data_trace_world_models(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_world_trace_desc *desc,
                                               slayer3d_game_data_world_trace_result *out_result);

    /**
     * @brief Pick an active world model and return editor-facing selection metadata.
     *
     * This is a convenience layer over @ref slayer3d_game_data_trace_world_models
     * for editor viewports. The returned selection includes the raw trace hit,
     * stable authored names and indexes, world-space bounds where available, and
     * pointers to runtime-owned editor metadata for the selected world, element,
     * material, face, and visible compiled render face when available. Pointers
     * remain valid until the runtime is destroyed or the brush world is rebuilt.
     */
    bool slayer3d_game_data_pick_editor_world_model(const slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_world_trace_desc *desc,
                                                    slayer3d_game_data_editor_selection *out_selection);

    /**
     * @brief Query which active world model volume contains a point.
     *
     * The first matching sector or brush volume in active-scene order is
     * returned. Set @p model_filter to a bitmask of
     * SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_* values, or 0 for all models.
     * Set @p brush_contents_mask to a bitmask of
     * SLAYER3D_GAME_DATA_BRUSH_CONTENT_* values for brush point queries, or 0
     * for every brush contents type.
     */
    bool slayer3d_game_data_query_world_model_point(const slayer3d_game_data_runtime *runtime, slayer3d_vec3 point,
                                                    unsigned int model_filter, unsigned int brush_contents_mask,
                                                    slayer3d_game_data_world_point_result *out_result);

    /**
     * @brief Copy generic world-model diagnostics for debug UI and editor tools.
     *
     * Instance counts reflect the currently active scene at call time. Trace
     * and point-query counts are cumulative until the runtime is destroyed.
     */
    bool slayer3d_game_data_get_world_model_diagnostics(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_world_model_diagnostics *out_diagnostics);

    /**
     * @brief Iterate renderer-agnostic editor/debug line primitives.
     *
     * The helper emits deterministic line segments for active world bounds,
     * current selection bounds, trace rays, selected face normals, and hit
     * markers. Editors can draw these through any backend, while runtime hosts
     * can use the companion presentation helper.
     */
    bool slayer3d_game_data_for_each_editor_debug_primitive(const slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_game_data_editor_debug_desc *desc,
                                                            slayer3d_game_data_editor_debug_primitive_fn callback,
                                                            void *userdata);

    /**
     * @brief Update authored active-scene editor tooling state.
     *
     * Scenes may author an `editor.selection` block with a trace descriptor and
     * scene-state output keys. This helper runs that generic pick query and
     * publishes stable selection metadata for UI inspectors. It is intended for
     * editor dojos and tools that run through the same managed loop as games.
     */
    bool slayer3d_game_data_update_active_editor_tooling(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Copy the active data-authored editor selection.
     *
     * Returns false and writes an empty selection when no object has been
     * selected in the active scene. Selection pointers are runtime-owned and
     * remain valid until the runtime is destroyed, reloaded, or the selected
     * brush world is rebuilt.
     */
    bool slayer3d_game_data_get_active_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_editor_selection *out_selection);

    /** @brief Runtime editor state for one mutable brush world. */
    typedef struct slayer3d_game_data_brush_world_editor_state
    {
        /** @brief Brush world name. Pointer is runtime-owned. */
        const char *world_name;
        /** @brief Last known host save/source path, or NULL when unknown. Pointer is runtime-owned. */
        const char *source_path;
        /** @brief True when runtime mutations have not been marked saved. */
        bool dirty;
        /** @brief Monotonic runtime mutation revision. */
        Uint64 revision;
        /** @brief Revision that was last marked saved. */
        Uint64 saved_revision;
    } slayer3d_game_data_brush_world_editor_state;

    /**
     * @brief Query editor save state for one runtime brush world.
     *
     * The returned pointers are runtime-owned and remain valid until the brush
     * world is saved/marked saved again or the runtime is destroyed.
     */
    bool slayer3d_game_data_get_brush_world_editor_state(const slayer3d_game_data_runtime *runtime,
                                                         const char *world_name,
                                                         slayer3d_game_data_brush_world_editor_state *out_state);

    /** @brief Maximum bytes, including the NUL terminator, for editor structural diagnostic text. */
#define SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX 256

    /** @brief Source-model structural diagnostics for one editable brush world. */
    typedef struct slayer3d_game_data_editor_brush_source_diagnostics
    {
        /** @brief True when the brush world has an editor-owned source model. */
        bool has_source_model;
        /** @brief True when no blocking structural defects were found. */
        bool structurally_valid;
        /** @brief Number of source boxes inspected. */
        int source_box_count;
        /** @brief Source-coordinate snap unit for this source world. */
        int source_snap_units;
        /** @brief Source boxes whose min/max coordinates are not aligned to source_snap_units. */
        int off_snap_count;
        /** @brief Positive-volume overlaps between structural source boxes. These are compile-time warnings. */
        int positive_overlap_count;
        /** @brief Tiny non-zero gaps between otherwise adjacent structural source boxes. These indicate seams/leaks. */
        int near_gap_count;
        /** @brief Exact face contacts between structural source boxes. */
        int face_contact_count;
        /** @brief Exact edge contacts between structural source boxes. */
        int edge_contact_count;
        /** @brief Exact vertex contacts between structural source boxes. */
        int vertex_contact_count;
        /** @brief Exact face contacts where only part of a structural source-box face is covered. */
        int partial_face_contact_count;
        /** @brief Runtime brushes currently compiled from the source model. */
        int runtime_brush_count;
        /** @brief Runtime brushes that do not map exactly back to one source box. */
        int runtime_source_mismatch_count;
        /** @brief Visible compiled render-face metadata entries. */
        int compiled_face_count;
        /** @brief Compiled render faces missing source brush or face identity metadata. */
        int compiled_face_missing_source_count;
        /** @brief Compiled render faces whose source identity does not resolve to the source model. */
        int compiled_face_unknown_source_count;
        /** @brief First source diagnostic issue or warning, or an empty string when none was found. */
        char first_issue[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Stable category for first_issue, or empty when no issue/warning was found. */
        char first_issue_kind[64];
        /** @brief Primary source brush name related to first_issue, or empty when not applicable. */
        char first_issue_source_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Primary source brush stable id related to first_issue, or empty when not applicable. */
        char first_issue_source_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Secondary source brush name related to first_issue, or empty when not applicable. */
        char first_issue_related_source_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Secondary source brush stable id related to first_issue, or empty when not applicable. */
        char first_issue_related_source_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Source face key or stable id related to first_issue, or empty when not applicable. */
        char first_issue_source_face[64];
        /** @brief Runtime brush name related to first_issue, or empty when not applicable. */
        char first_issue_runtime_brush_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Runtime brush index related to first_issue, or -1 when not applicable. */
        int first_issue_runtime_brush_index;
        /** @brief Compiled render-face metadata index related to first_issue, or -1 when not applicable. */
        int first_issue_compiled_face_index;
    } slayer3d_game_data_editor_brush_source_diagnostics;

    /**
     * @brief Validate source-box topology for an editable brush world.
     *
     * This pass operates on canonical `editor_brush_sources` integer/fixed
     * coordinates, not derived runtime triangles. It reports structural defects
     * that can create seams or z-fighting before the map is compiled for play.
     * Non-structural content such as trigger-only boxes is ignored by topology
     * checks so gameplay volumes can overlap sealed architecture.
     * Source worlds may author `snap_units` to make off-grid source coordinates
     * a blocking structural defect.
     *
     * Source-backed worlds also validate compile identity: runtime brushes must
     * map back to source boxes, and every visible compiled render face must carry
     * source brush/face metadata that resolves to the source model. This keeps
     * selection, save/open, diagnostics, and compiled rendering tied to the same
     * structural truth.
     *
     * @p near_gap_units controls how many source units count as a suspicious
     * near miss between otherwise overlapping boxes. Pass 0 to use the default
     * tolerance of one source unit.
     */
    bool slayer3d_game_data_validate_editor_brush_source_model(
        const slayer3d_game_data_runtime *runtime, const char *world_name, int near_gap_units,
        slayer3d_game_data_editor_brush_source_diagnostics *out_diagnostics, char *error_buffer, int error_buffer_size);

    /** @brief Source-model playable-space leak diagnostics for one player start. */
    typedef struct slayer3d_game_data_editor_brush_enclosure_diagnostics
    {
        /** @brief True when the brush world has an editor-owned source model. */
        bool has_source_model;
        /** @brief True when the named player start exists. */
        bool has_player_start;
        /** @brief True when the player-start reachable empty space does not reach outside the source bounds. */
        bool enclosed;
        /** @brief Number of source boxes inspected. */
        int source_box_count;
        /** @brief Total source flood-grid cells inspected. */
        int grid_cell_count;
        /** @brief Number of grid cells marked solid by structural source boxes. */
        int solid_cell_count;
        /** @brief Number of empty cells reachable from the player-start cell. */
        int visited_cell_count;
        /** @brief Number of outside-boundary cells reached by the flood. Non-zero means leaked/open. */
        int open_boundary_cell_count;
        /** @brief First reachable outside-boundary cell center in world meters. */
        slayer3d_vec3 first_leak_point;
        /** @brief Axis of the first reachable outside-boundary cell, or empty. */
        char first_leak_axis[8];
        /** @brief Side of the first reachable outside-boundary cell, either "negative", "positive", or empty. */
        char first_leak_side[16];
        /** @brief Nearest source-box name for the first leak boundary, or empty. */
        char candidate_source_name[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Nearest source-box stable id for the first leak boundary, or empty. */
        char candidate_source_stable_id[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
        /** @brief Nearest source-box face key for the first leak boundary, or empty. */
        char candidate_source_face[8];
        /** @brief Nearest point on the candidate source face in world meters. */
        slayer3d_vec3 candidate_source_point;
        /** @brief Distance in meters from the first leak point to the candidate source face. */
        float candidate_source_distance;
        /** @brief First blocking issue, or an empty string when enclosed. */
        char first_issue[SLAYER3D_GAME_DATA_EDITOR_DIAGNOSTIC_TEXT_MAX];
    } slayer3d_game_data_editor_brush_enclosure_diagnostics;

    /**
     * @brief Validate source-box playable-space closure from one editor player start.
     *
     * This pass derives an exact interval grid from fixed-coordinate
     * `editor_brush_sources`, marks structural source boxes as solid, and
     * flood-fills empty space from @p player_start_name. If reachable empty space reaches the
     * expanded outside boundary, the map is considered open/leaking for MVP
     * grid-prefab test-run workflows.
     *
     * @p max_cells bounds diagnostic cost. Pass 0 to use the default cap.
     */
    bool slayer3d_game_data_validate_editor_brush_source_enclosure(
        const slayer3d_game_data_runtime *runtime, const char *world_name, const char *player_start_name, int max_cells,
        slayer3d_game_data_editor_brush_enclosure_diagnostics *out_diagnostics, char *error_buffer,
        int error_buffer_size);

    /**
     * @brief Mark one runtime brush world as saved by an editor host.
     *
     * @p source_path may be NULL to keep the existing source path, or a
     * filesystem path to associate with the saved revision.
     */
    bool slayer3d_game_data_mark_brush_world_saved(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                   const char *source_path, char *error_buffer, int error_buffer_size);

    /** @brief Descriptor for creating one axis-aligned convex box brush. */
    typedef struct slayer3d_game_data_create_box_brush_desc
    {
        /** @brief Target brush world name. Required. */
        const char *world_name;
        /** @brief Optional brush name. If NULL/empty, a unique editor name is generated. */
        const char *brush_name;
        /** @brief Brush material assigned to all six faces. Required. */
        const char *material_name;
        /** @brief Minimum XYZ corner. Each component must be less than @p max. */
        slayer3d_vec3 min;
        /** @brief Maximum XYZ corner. Each component must be greater than @p min. */
        slayer3d_vec3 max;
        /** @brief Brush contents bitmask. Zero defaults to solid. */
        unsigned int contents;
    } slayer3d_game_data_create_box_brush_desc;

    /**
     * @brief Append one axis-aligned box brush to a runtime brush world.
     *
     * The operation is atomic from the runtime caller's perspective: the brush
     * is visible only after allocations, acceleration rebuild, and render-model
     * compilation all succeed. Success marks the brush world dirty and
     * increments its editor revision. Structural box brushes may touch existing
     * structural brushes exactly, but positive-volume overlap is rejected.
     * Source-backed worlds commit a canonical source box first and rebuild
     * runtime brushes from that source. Runtime-only worlds receive stable
     * editor metadata on the brush and generated faces. @p out_brush_name
     * receives the final runtime brush name when non-NULL.
     */
    bool slayer3d_game_data_create_box_brush(slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_create_box_brush_desc *desc, char *out_brush_name,
                                             size_t out_brush_name_size, char *error_buffer, int error_buffer_size);

    /** @brief Runtime-authored player start marker for editor and test-run workflows. */
    typedef struct slayer3d_game_data_editor_player_start
    {
        /** @brief Stable player start name. Pointer is runtime-owned. */
        const char *name;
        /** @brief Scene where this start is valid, or NULL for scene-agnostic starts. */
        const char *scene;
        /** @brief Optional actor/entity to place when test-running from this start. */
        const char *target;
        /** @brief Spawn position in world meters. */
        slayer3d_vec3 position;
        /** @brief Spawn yaw in radians. */
        float yaw;
        /** @brief Spawn pitch in radians. */
        float pitch;
    } slayer3d_game_data_editor_player_start;

    /** @brief Editor save state for the player-start collection. */
    typedef struct slayer3d_game_data_player_start_editor_state
    {
        /** @brief Last known host save/source path, or NULL when unknown. Pointer is runtime-owned. */
        const char *source_path;
        /** @brief True when runtime mutations have not been marked saved. */
        bool dirty;
        /** @brief Monotonic runtime mutation revision. */
        Uint64 revision;
        /** @brief Revision that was last marked saved. */
        Uint64 saved_revision;
        /** @brief Number of player starts currently loaded in the runtime. */
        int count;
    } slayer3d_game_data_player_start_editor_state;

    /** @brief Descriptor for creating or updating one editor player start. */
    typedef struct slayer3d_game_data_place_player_start_desc
    {
        /** @brief Player start name. Required. */
        const char *name;
        /** @brief Optional scene reference. Defaults to the active scene when omitted. */
        const char *scene;
        /** @brief Optional actor/entity to place when applying the start. */
        const char *target;
        /** @brief Spawn position. Used only when @p has_position is true. */
        slayer3d_vec3 position;
        /** @brief Whether @p position is explicit. Defaults to selection point, then target actor position. */
        bool has_position;
        /** @brief Spawn yaw in radians. Used only when @p has_yaw is true. */
        float yaw;
        /** @brief Whether @p yaw is explicit. */
        bool has_yaw;
        /** @brief Spawn pitch in radians. Used only when @p has_pitch is true. */
        float pitch;
        /** @brief Whether @p pitch is explicit. */
        bool has_pitch;
        /** @brief Apply the start to @p target immediately when a target actor exists. */
        bool apply_to_target;
    } slayer3d_game_data_place_player_start_desc;

    /**
     * @brief Query one editor player start by name.
     *
     * Returned pointers are runtime-owned and remain valid until player starts
     * are mutated or the runtime is destroyed.
     */
    bool slayer3d_game_data_get_editor_player_start(const slayer3d_game_data_runtime *runtime, const char *name,
                                                    slayer3d_game_data_editor_player_start *out_start);

    /** @brief Query editor save state for runtime player-start markers. */
    bool slayer3d_game_data_get_player_start_editor_state(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_game_data_player_start_editor_state *out_state);

    /**
     * @brief Create or update one runtime editor player start.
     *
     * The operation marks the player-start collection dirty and increments its
     * revision. When @p apply_to_target is true and the target actor exists,
     * the actor position/yaw/pitch are updated atomically after the marker is
     * stored.
     */
    bool slayer3d_game_data_place_editor_player_start(slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_place_player_start_desc *desc,
                                                      char *error_buffer, int error_buffer_size);

    /**
     * @brief Apply one editor player start to its target actor.
     *
     * The start must exist and define a target actor. The target actor position
     * and yaw/pitch properties are updated to match the stored marker. This is
     * intended for editor test-run and direct-start workflows.
     */
    bool slayer3d_game_data_apply_editor_player_start(slayer3d_game_data_runtime *runtime, const char *name,
                                                      char *error_buffer, int error_buffer_size);

    /**
     * @brief Export all runtime editor player starts as a fragment JSON string.
     *
     * The caller owns @p out_json and must release it with SDL_free().
     */
    bool slayer3d_game_data_export_player_starts_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                               char **out_json, size_t *out_size, char *error_buffer,
                                                               int error_buffer_size);

    /**
     * @brief Mark the runtime editor player-start collection as saved.
     *
     * @p source_path may be NULL to keep the existing source path, or a
     * filesystem path to associate with the saved revision.
     */
    bool slayer3d_game_data_mark_player_starts_saved(slayer3d_game_data_runtime *runtime, const char *source_path,
                                                     char *error_buffer, int error_buffer_size);

    /** @brief Descriptor for resizing one brush face plane. */
    typedef struct slayer3d_game_data_resize_brush_face_desc
    {
        /** @brief Target brush world name. Required. */
        const char *world_name;
        /** @brief Target brush name. Required. */
        const char *brush_name;
        /** @brief Zero-based face index on the target brush. */
        int face_index;
        /** @brief Signed face-plane distance. Positive expands the brush outward. */
        float distance;
    } slayer3d_game_data_resize_brush_face_desc;

    /**
     * @brief Move one brush face plane along its normal.
     *
     * Positive distances grow the brush outward along the selected face normal;
     * negative distances shrink it. The operation rebuilds brush collision and
     * render data before committing and rolls back if the result is invalid.
     * Success marks the brush world dirty and increments its editor revision.
     */
    bool slayer3d_game_data_resize_brush_face(slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_resize_brush_face_desc *desc, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Iterate data-authored editor debug primitives for the active scene.
     *
     * This reads the active scene's `editor.debug_overlay` and
     * `editor.selection` blocks, performs the authored trace when present, and
     * emits world bounds, selected bounds, trace rays, face normals, and hit
     * markers using the same primitive callback contract as
     * @ref slayer3d_game_data_for_each_editor_debug_primitive.
     */
    bool slayer3d_game_data_for_each_active_editor_debug_primitive(
        const slayer3d_game_data_runtime *runtime, slayer3d_game_data_editor_debug_primitive_fn callback,
        void *userdata);

    /**
     * @brief Export one runtime brush world as a canonical JSON fragment.
     *
     * The exported document uses `schema: "slayer3d.fragment.v0"` and contains a
     * single `brush_worlds` entry. Runtime editor mutations such as brush
     * translation and face material painting are reflected in the exported
     * planes and material references. The returned string is allocated with
     * SDL_malloc and must be released with SDL_free().
     */
    bool slayer3d_game_data_export_brush_world_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                             const char *world_name, char **out_json, size_t *out_size,
                                                             char *error_buffer, int error_buffer_size);

    /**
     * @brief Export a JSON manifest describing one compiled brush-world artifact.
     *
     * The exported document uses `schema:
     * "slayer3d.brush_compile_artifact.v0"` and records a deterministic source
     * hash for authored brush inputs, the compile policy, the compiled-artifact
     * hash, editor source-model metadata when present, render mesh totals,
     * spatial chunk metadata, and visibility-grid metadata. This is an inspection
     * and cache-invalidation descriptor; it does not contain the binary mesh or
     * collision payloads needed to load a compiled artifact directly. The returned
     * string is allocated with SDL_malloc and must be released with SDL_free().
     */
    bool slayer3d_game_data_export_brush_world_compile_artifact_json(const slayer3d_game_data_runtime *runtime,
                                                                     const char *world_name, char **out_json,
                                                                     size_t *out_size, char *error_buffer,
                                                                     int error_buffer_size);

    /**
     * @brief Verify one brush compile artifact manifest against the current runtime world.
     *
     * This helper checks the descriptor JSON produced by
     * @ref slayer3d_game_data_export_brush_world_compile_artifact_json without
     * loading any binary cache payload. A return value of true means the manifest
     * was parsed and compared; inspect @p out_status->fresh to decide whether an
     * offline artifact is reusable. Source-backed editable worlds also compare
     * editor source-model metadata so tooling can detect runtime-only or stale
     * source-box manifests before trusting cached output. Stale manifests are
     * reported through @p out_status rather than treated as API errors.
     */
    bool slayer3d_game_data_verify_brush_world_compile_artifact_json(
        const slayer3d_game_data_runtime *runtime, const char *world_name, const char *json, size_t json_size,
        slayer3d_game_data_brush_compile_artifact_status *out_status, char *error_buffer, int error_buffer_size);

    /**
     * @brief Verify a brush compile artifact manifest file against the current runtime world.
     *
     * The file is read as JSON and compared using
     * @ref slayer3d_game_data_verify_brush_world_compile_artifact_json. Missing,
     * unreadable, or malformed files return false; valid-but-stale manifests
     * return true with @p out_status->fresh set to false.
     */
    bool slayer3d_game_data_verify_brush_world_compile_artifact_file(
        const slayer3d_game_data_runtime *runtime, const char *world_name, const char *path,
        slayer3d_game_data_brush_compile_artifact_status *out_status, char *error_buffer, int error_buffer_size);

    /**
     * @brief Atomically save one brush compile artifact manifest file.
     *
     * This writes the same descriptor JSON produced by
     * @ref slayer3d_game_data_export_brush_world_compile_artifact_json. Parent
     * directories are created automatically. The manifest is intended for
     * editor/offline compiler inspection and future cache invalidation, not as a
     * binary artifact payload.
     */
    bool slayer3d_game_data_save_brush_world_compile_artifact_file(const slayer3d_game_data_runtime *runtime,
                                                                   const char *world_name, const char *path,
                                                                   size_t *out_size, char *error_buffer,
                                                                   int error_buffer_size);

    /**
     * @brief Resolve the canonical offline artifact layout for one brush world.
     *
     * @p artifact_root must be a native filesystem directory path, not an
     * `asset://` or other virtual URI. The resolved layout is versioned as
     * `brush/v0/<world-key>/<source-hash>/<compile-artifact-hash>/...`.
     * The manifest path is usable with
     * @ref slayer3d_game_data_save_brush_world_compile_artifact_file. Binary
     * payload paths are reserved for future offline mesh/collision cache data;
     * the runtime still rebuilds brush artifacts from authored source.
     */
    bool slayer3d_game_data_get_brush_world_compile_artifact_layout(
        const slayer3d_game_data_runtime *runtime, const char *world_name, const char *artifact_root,
        slayer3d_game_data_brush_compile_artifact_layout *out_layout, char *error_buffer, int error_buffer_size);

    /**
     * @brief Atomically save one brush compile artifact manifest using the canonical layout.
     *
     * This resolves the layout with
     * @ref slayer3d_game_data_get_brush_world_compile_artifact_layout and writes
     * the JSON manifest to `out_layout->manifest_path`. Parent directories are
     * created automatically. Passing NULL for @p out_layout is allowed.
     */
    bool slayer3d_game_data_save_brush_world_compile_artifact_layout(
        const slayer3d_game_data_runtime *runtime, const char *world_name, const char *artifact_root,
        slayer3d_game_data_brush_compile_artifact_layout *out_layout, size_t *out_size, char *error_buffer,
        int error_buffer_size);

    /**
     * @brief Atomically save one runtime brush world as a JSON fragment file.
     *
     * This is the filesystem-facing companion to
     * @ref slayer3d_game_data_export_brush_world_fragment_json for editor
     * hosts. Parent directories are created automatically. The write uses a
     * temporary file in the target directory and renames it into place, so
     * callers never observe a partially written fragment. On success the brush
     * world is marked saved and @p path becomes its editor source path.
     */
    bool slayer3d_game_data_save_brush_world_fragment_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                           const char *path, size_t *out_size, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Export one editable level fragment containing brushes, source brushes, and starts.
     *
     * The exported document uses `schema: "slayer3d.fragment.v0"` and contains
     * the selected `brush_worlds` entry, a fixed-coordinate
     * `editor_brush_sources` snapshot for structural editor workflows, and the
     * runtime `editor_player_starts` collection. When this fragment is loaded
     * again, matching `editor_brush_sources` are compiled back into the runtime
     * brush world, making the fixed-coordinate source boxes the authoritative
     * editable geometry for supported box brushes. Export fails when the target
     * world is not source-backed or when source/compiled identity validation
     * reports a blocking structural defect. The returned string is allocated
     * with SDL_malloc and must be released with SDL_free().
     */
    bool slayer3d_game_data_export_editable_level_fragment_json(const slayer3d_game_data_runtime *runtime,
                                                                const char *world_name, char **out_json,
                                                                size_t *out_size, char *error_buffer,
                                                                int error_buffer_size);

    /**
     * @brief Atomically save one editable level fragment file.
     *
     * This saves the same JSON produced by
     * @ref slayer3d_game_data_export_editable_level_fragment_json. Parent
     * directories are created automatically, and the target path is updated via
     * a same-directory temporary file. On success, both the selected brush world
     * and the player-start collection are marked saved at their current
     * revisions and @p path becomes their editor source path.
     */
    bool slayer3d_game_data_save_editable_level_fragment_file(slayer3d_game_data_runtime *runtime,
                                                              const char *world_name, const char *path,
                                                              size_t *out_size, char *error_buffer,
                                                              int error_buffer_size);

    /**
     * @brief Load an editable level fragment JSON buffer into an existing editor runtime.
     *
     * This performs the same import as
     * @ref slayer3d_game_data_load_editable_level_fragment_file, but reads from
     * an already loaded JSON buffer. Use this for in-memory editor workflows,
     * tests, and browser builds where the caller owns file selection/storage.
     * When @p source_path is non-null and non-empty, it becomes the clean
     * editor source path for the imported brush world and player starts.
     */
    bool slayer3d_game_data_load_editable_level_fragment_json(slayer3d_game_data_runtime *runtime,
                                                              const char *world_name, const void *json,
                                                              size_t json_size, const char *source_path,
                                                              char *error_buffer, int error_buffer_size);

    /**
     * @brief Load an editable level fragment file into an existing editor runtime.
     *
     * The input must be a `slayer3d.fragment.v0` document containing a
     * `brush_worlds` entry whose name matches @p world_name and a matching
     * `editor_brush_sources` entry. The matching world replaces the runtime
     * world in place; `editor_brush_sources` is the canonical fixed-coordinate
     * editor source model for the same world and is compiled into runtime
     * brushes during import. Runtime-only brush fragments are rejected for
     * editable graybox workflows. `editor_player_starts` from the fragment
     * replaces the runtime player-start collection. On success both runtime
     * collections are marked clean and @p path becomes their editor source path.
     */
    bool slayer3d_game_data_load_editable_level_fragment_file(slayer3d_game_data_runtime *runtime,
                                                              const char *world_name, const char *path,
                                                              char *error_buffer, int error_buffer_size);

    /** @brief Descriptor for creating an editor test-run handoff manifest. */
    typedef struct slayer3d_game_data_editor_test_run_desc
    {
        /** @brief Root game-data asset path to pass to the generic runner. Required. */
        const char *data_asset_path;
        /** @brief Optional scene to direct-start. Must match the player start scene when both are set. */
        const char *scene;
        /** @brief Optional editor player start to apply before scene enter. */
        const char *player_start;
    } slayer3d_game_data_editor_test_run_desc;

    /**
     * @brief Export a small JSON handoff manifest for editor test-run workflows.
     *
     * The manifest uses `schema: "slayer3d.editor_test_run.v0"` and contains
     * the runner data asset, resolved scene when known, player start when
     * provided, and a runner argument array excluding mount flags. Editor hosts
     * combine this with their current `--root`, `--pack`, or fused executable
     * context to launch the generic runner without game-specific native code. If
     * the runtime has source-backed brush worlds, their source/compiled identity
     * must validate before a test-run manifest can be exported. The returned
     * string is allocated with SDL_malloc and must be released with SDL_free().
     */
    bool slayer3d_game_data_export_editor_test_run_manifest_json(const slayer3d_game_data_runtime *runtime,
                                                                 const slayer3d_game_data_editor_test_run_desc *desc,
                                                                 char **out_json, size_t *out_size, char *error_buffer,
                                                                 int error_buffer_size);

    /** @brief Authored game data diagnostic severity. */
    typedef enum slayer3d_game_data_diagnostic_severity
    {
        /** @brief Non-fatal issue that authors should review. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal issue that prevents the data from loading. */
        SLAYER3D_GAME_DATA_DIAGNOSTIC_ERROR = 2,
    } slayer3d_game_data_diagnostic_severity;

    /**
     * @brief Callback for authored game data validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the authored object or field
     * that produced the diagnostic. @p message is a human-readable description
     * intended to be actionable without stepping through engine code.
     */
    typedef void (*slayer3d_game_data_diagnostic_fn)(void *userdata, slayer3d_game_data_diagnostic_severity severity,
                                                     const char *json_path, const char *message);

    /**
     * @brief Options controlling authored game data validation.
     */
    typedef struct slayer3d_game_data_validation_options
    {
        /** @brief Optional diagnostic callback. */
        slayer3d_game_data_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } slayer3d_game_data_validation_options;

    /**
     * @brief Named game-specific callback invoked by JSON actions/components.
     *
     * @p adapter_name is the authored adapter name. @p target is the resolved
     * target actor when the JSON supplied one, otherwise NULL. @p payload is the
     * signal payload that caused the invocation for action adapters. Component
     * adapters receive a small authored payload, such as target_actor_name for
     * controller components.
     *
     * @return true when the adapter recognized and applied the request.
     */
    typedef bool (*slayer3d_game_data_adapter_fn)(void *userdata, slayer3d_game_data_runtime *runtime,
                                                  const char *adapter_name, slayer3d_registered_actor *target,
                                                  const slayer3d_properties *payload);

    /**
     * @brief Load a JSON game data file into a session.
     *
     * The session must provide an actor registry, signal bus, timer pool, and
     * input manager when the corresponding JSON sections are used. The runtime
     * owns the parsed JSON document and any signal bindings it installs; destroy
     * it before destroying the session services.
     *
     * @param path JSON file path.
     * @param session Target session whose services receive the authored data.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success.
     */
    bool slayer3d_game_data_load_file(const char *path, slayer3d_game_session *session,
                                      slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                      int error_buffer_size);

    /**
     * @brief Optional game-data load-time overrides.
     *
     * Hosts normally load the authored `scenes.initial` scene and emit that
     * scene's enter signal. Development tools and editors can provide
     * `initial_scene_override` to enter a different scene before any enter
     * signal fires. `initial_scene_state` is copied into the persistent
     * scene-state bag before the first enter signal; `initial_scene_payload`
     * is passed only to that initial scene-enter signal. `initial_player_start`
     * applies an editor-authored player start before camera setup and the first
     * enter signal; when it has a scene and no explicit scene override is set,
     * that scene becomes the initial scene.
     */
    typedef struct slayer3d_game_data_load_options
    {
        /** @brief Game session that receives authored signals, timers, and input bindings. Required. */
        slayer3d_game_session *session;
        /** @brief Optional authored scene name to enter instead of `scenes.initial`. */
        const char *initial_scene_override;
        /** @brief Optional persistent scene-state values copied before first scene enter. */
        const slayer3d_properties *initial_scene_state;
        /** @brief Optional transient payload passed to the first scene-enter signal. */
        const slayer3d_properties *initial_scene_payload;
        /** @brief Optional editor player start to apply for direct test-run workflows. */
        const char *initial_player_start;
    } slayer3d_game_data_load_options;

    /**
     * @brief Load a JSON game data asset through a resolver.
     *
     * This is the preferred loading entry point for games that may ship data in
     * source directories, packed archives, or embedded packs. Script paths in
     * the JSON are resolved relative to @p asset_path through the same resolver.
     * The runtime borrows @p assets for later runtime asset actions, so callers
     * must keep the resolver alive until the runtime is destroyed.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param session Target session whose services receive the authored data.
     * @param out_runtime Receives the created runtime on success.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true on success.
     */
    bool slayer3d_game_data_load_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                       slayer3d_game_session *session, slayer3d_game_data_runtime **out_runtime,
                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Load a JSON game data asset through a resolver with load-time overrides.
     *
     * This uses the same resolver and ownership rules as
     * @ref slayer3d_game_data_load_asset, with the additional ability to choose
     * the first active scene and seed scene state before any scene-enter signal
     * runs.
     */
    bool slayer3d_game_data_load_asset_with_options(slayer3d_asset_resolver *assets, const char *asset_path,
                                                    const slayer3d_game_data_load_options *options,
                                                    slayer3d_game_data_runtime **out_runtime, char *error_buffer,
                                                    int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data asset.
     *
     * This lightweight reader is intended for startup, before a managed loop
     * creates a window or game session. Missing fields keep the values already
     * present in @p out_config, so callers can initialize defaults first. When
     * an authored title is present, it is copied into @p title_buffer and
     * out_config->title points at that buffer.
     */
    bool slayer3d_game_data_load_app_config_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                                  slayer3d_game_config *out_config, char *title_buffer,
                                                  int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data file.
     *
     * Missing fields keep the values already present in @p out_config, so
     * callers can initialize defaults first. When an authored title is present,
     * it is copied into @p title_buffer and out_config->title points at that
     * buffer.
     */
    bool slayer3d_game_data_load_app_config_file(const char *path, slayer3d_game_config *out_config, char *title_buffer,
                                                 int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Get the writable storage identity authored by the game data.
     *
     * The returned config is suitable for slayer3d_storage_create(). String
     * pointers are owned by @p runtime and remain valid until
     * slayer3d_game_data_destroy(). If the JSON omits the storage block, SLAYER3D
     * derives conservative defaults from metadata/app fields and finally falls
     * back to slayer3d_storage_config_init() defaults.
     *
     * @param runtime Loaded game data runtime.
     * @param out_config Receives the resolved storage configuration.
     * @return true when @p out_config was filled.
     */
    bool slayer3d_game_data_get_storage_config(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_storage_config *out_config);

    /**
     * @brief Return whether the game data authored a network replication schema.
     *
     * Local-only games can omit the `network` block entirely. In that case
     * there is no schema hash to exchange during multiplayer handshakes.
     */
    bool slayer3d_game_data_has_network_schema(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Copy the deterministic network replication schema hash.
     *
     * The hash covers the authored protocol, replication channels, fields,
     * inputs, and control messages, but ignores unrelated game data. It is
     * intended for host/client compatibility checks before gameplay begins.
     *
     * @return true when @p runtime has an authored network schema and
     * @p out_hash was filled with SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE bytes.
     */
    bool slayer3d_game_data_get_network_schema_hash(const slayer3d_game_data_runtime *runtime,
                                                    Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE]);

    /**
     * @brief Resolve an authored scene-state key used by network UI/session flows.
     *
     * Games may place arbitrary string key maps under `network.scene_state`.
     * Host integration code can use this helper to avoid hard-coding the
     * `scene_state` property names that authored lobby, discovery, or direct
     * connect scenes display.
     *
     * For example, `network.scene_state.host.status` can resolve to
     * `multiplayer_host_status`.
     *
     * @param runtime Loaded game data runtime.
     * @param scope Authored scene-state group, such as `host`.
     * @param name Authored key name within the group, such as `status`.
     * @param out_key Receives a string owned by @p runtime.
     * @return true when the key exists and @p out_key was filled.
     */
    bool slayer3d_game_data_get_network_scene_state_key(const slayer3d_game_data_runtime *runtime, const char *scope,
                                                        const char *name, const char **out_key);

    /**
     * @brief Resolve an authored network session scene id.
     *
     * Games may author reusable scene ids under `network.session_flow.scenes`,
     * such as `play`, `host_lobby`, `join`, `direct_connect`, or `title`.
     * Host integration code can use this helper to avoid hard-coding scene ids
     * while still owning transport/session objects.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored scene semantic name.
     * @param out_scene Receives a scene id owned by @p runtime.
     * @return true when the scene semantic exists and @p out_scene was filled.
     */
    bool slayer3d_game_data_get_network_session_scene(const slayer3d_game_data_runtime *runtime, const char *name,
                                                      const char **out_scene);

    /**
     * @brief Resolve an authored network session scene-state key.
     *
     * Keys are authored under `network.session_flow.state_keys`, such as
     * `match_mode`, `network_role`, or `network_flow`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored key semantic name.
     * @param out_key Receives a scene-state key owned by @p runtime.
     * @return true when the key semantic exists and @p out_key was filled.
     */
    bool slayer3d_game_data_get_network_session_state_key(const slayer3d_game_data_runtime *runtime, const char *name,
                                                          const char **out_key);

    /**
     * @brief Resolve an authored network session scene-state value.
     *
     * Values are authored under `network.session_flow.state_values.<group>`.
     * For example, `state_values.network_role.host` may resolve to `host`.
     *
     * @param runtime Loaded game data runtime.
     * @param group Authored value group, usually a state key semantic.
     * @param name Authored value semantic name.
     * @param out_value Receives a value string owned by @p runtime.
     * @return true when the value semantic exists and @p out_value was filled.
     */
    bool slayer3d_game_data_get_network_session_state_value(const slayer3d_game_data_runtime *runtime,
                                                            const char *group, const char *name,
                                                            const char **out_value);

    /**
     * @brief Resolve an authored network session message.
     *
     * Messages are authored under `network.session_flow.messages.<group>`.
     * Host integration code can use this for player-facing network flow text
     * such as disconnect reasons or termination prompts without hard-coding
     * those strings in C.
     *
     * @param runtime Loaded game data runtime.
     * @param group Authored message group.
     * @param name Authored message semantic name.
     * @param out_message Receives a message string owned by @p runtime.
     * @return true when the message semantic exists and @p out_message was filled.
     */
    bool slayer3d_game_data_get_network_session_message(const slayer3d_game_data_runtime *runtime, const char *group,
                                                        const char *name, const char **out_message);

    /**
     * @brief Return whether authored managed network orchestration is enabled.
     *
     * Managed networking is enabled by `network.session_flow.managed_runtime.enabled`.
     * Hosts may still choose not to run it; this helper only reports the
     * authored game-data policy.
     *
     * @param runtime Loaded game data runtime.
     * @return true when the game authors managed runtime networking as enabled.
     */
    bool slayer3d_game_data_network_managed_runtime_enabled(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Resolve the authored delay before acknowledging a terminated network match.
     *
     * The value is authored at
     * `network.session_flow.managed_runtime.termination_ack_delay_seconds`.
     *
     * @param runtime Loaded game data runtime.
     * @param out_seconds Receives the non-negative delay in seconds.
     * @return true when the delay is authored and @p out_seconds was filled.
     */
    bool slayer3d_game_data_get_network_managed_termination_ack_delay(const slayer3d_game_data_runtime *runtime,
                                                                      float *out_seconds);

    /**
     * @brief Test whether a scene keeps a managed network session alive.
     *
     * Scene semantics are authored under
     * `network.session_flow.managed_runtime.keep_alive_scenes.<session>`.
     * Each entry references a semantic from `network.session_flow.scenes`; this
     * helper resolves those semantics and compares them with @p scene_name.
     *
     * @param runtime Loaded game data runtime.
     * @param session_name Managed session semantic, such as `host` or
     * `direct_connect`.
     * @param scene_name Concrete active scene id to test.
     * @return true when the session should stay alive in the scene.
     */
    bool slayer3d_game_data_network_managed_keep_alive_scene_matches(const slayer3d_game_data_runtime *runtime,
                                                                     const char *session_name, const char *scene_name);

    /**
     * @brief Execute an authored network session-flow event.
     *
     * Events are authored under `network.session_flow.events.<name>`. An event
     * may be either an action array or an object with optional `pause` and
     * `actions` fields. The action array uses the same data action vocabulary
     * as logic bindings and scene activity actions. String values in supported
     * actions can reference string payload fields with `{field}` placeholders.
     *
     * @param runtime Loaded game data runtime.
     * @param ctx Optional game context; required only when the event authors a
     * `pause` field.
     * @param name Authored event semantic name.
     * @param payload Optional payload passed to event actions.
     * @param error_buffer Optional buffer for a failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the event exists and all actions execute.
     */
    bool slayer3d_game_data_run_network_session_flow_event(slayer3d_game_data_runtime *runtime,
                                                           slayer3d_game_context *ctx, const char *name,
                                                           const slayer3d_properties *payload, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Resolve an authored network runtime replication channel binding.
     *
     * Runtime bindings are authored under `network.runtime_bindings.replication`
     * and map host integration semantics, such as `state_snapshot` or
     * `client_input`, to concrete replication channel names declared in
     * `network.replication`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_channel Receives a replication channel name owned by @p runtime.
     * @return true when the binding exists and @p out_channel was filled.
     */
    bool slayer3d_game_data_get_network_runtime_replication(const slayer3d_game_data_runtime *runtime, const char *name,
                                                            const char **out_channel);

    /**
     * @brief Resolve an authored network runtime control-message binding.
     *
     * Runtime bindings are authored under `network.runtime_bindings.controls`
     * and map host integration semantics, such as `pause_request` or
     * `disconnect`, to concrete control-message names declared in
     * `network.control_messages`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_control Receives a control-message name owned by @p runtime.
     * @return true when the binding exists and @p out_control was filled.
     */
    bool slayer3d_game_data_get_network_runtime_control(const slayer3d_game_data_runtime *runtime, const char *name,
                                                        const char **out_control);

    /**
     * @brief Resolve the semantic runtime binding for an authored control message.
     *
     * This is the reverse lookup for
     * @ref slayer3d_game_data_get_network_runtime_control. It lets generic
     * network loops decode a control packet and dispatch on the authored
     * runtime semantic, such as `pause_request` or `disconnect`, without
     * knowing the concrete control-message name in the wire schema.
     *
     * @param runtime Loaded game data runtime.
     * @param control_name Authored control message name from `network.control_messages`.
     * @param out_binding Receives the semantic binding name owned by @p runtime.
     * @return true when a runtime control binding maps to @p control_name.
     */
    bool slayer3d_game_data_get_network_runtime_control_binding(const slayer3d_game_data_runtime *runtime,
                                                                const char *control_name, const char **out_binding);

    /**
     * @brief Resolve an authored network runtime input action binding.
     *
     * Runtime action bindings are authored under
     * `network.runtime_bindings.actions` and map host integration semantics,
     * such as `menu_back` or `camera_toggle`, to concrete input action names.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_action Receives the resolved action id.
     * @return true when the binding exists and resolves to an input action.
     */
    bool slayer3d_game_data_get_network_runtime_action(const slayer3d_game_data_runtime *runtime, const char *name,
                                                       int *out_action);

    /**
     * @brief Resolve an authored network runtime signal binding.
     *
     * Runtime signal bindings are authored under
     * `network.runtime_bindings.signals` and map host integration semantics,
     * such as `lobby_start` or `ui_select`, to concrete signal names.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_signal Receives the resolved signal id.
     * @return true when the binding exists and resolves to a signal.
     */
    bool slayer3d_game_data_get_network_runtime_signal(const slayer3d_game_data_runtime *runtime, const char *name,
                                                       int *out_signal);

    /**
     * @brief Return the number of authored haptics policies.
     *
     * Policies are authored under `haptics.policies`.
     *
     * @param runtime Loaded game data runtime.
     * @return Number of authored policies, or 0 when none are authored.
     */
    int slayer3d_game_data_haptics_policy_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored haptics policy descriptor by index.
     *
     * This exposes the policy's signal and rumble parameters so hosts can
     * subscribe to the necessary signals. Use
     * `slayer3d_game_data_match_haptics_policy()` before playing rumble.
     *
     * @param runtime Loaded game data runtime.
     * @param index Zero-based policy index.
     * @param out_policy Receives the policy descriptor.
     * @return true when @p index exists and @p out_policy was filled.
     */
    bool slayer3d_game_data_get_haptics_policy_at(const slayer3d_game_data_runtime *runtime, int index,
                                                  slayer3d_game_data_haptics_policy *out_policy);

    /**
     * @brief Test whether an authored haptics policy matches a signal event.
     *
     * The helper checks the policy signal, optional `enabled_if` condition, and
     * optional payload actor filters. Payload actor filters read actor names
     * from the signal payload and may match by concrete actor name or authored
     * entity tags.
     *
     * @param runtime Loaded game data runtime.
     * @param index Zero-based policy index.
     * @param signal_id Emitted signal id.
     * @param payload Optional signal payload.
     * @param out_policy Receives the matching policy descriptor.
     * @return true when the policy exists and matches this event.
     */
    bool slayer3d_game_data_match_haptics_policy(const slayer3d_game_data_runtime *runtime, int index, int signal_id,
                                                 const slayer3d_properties *payload,
                                                 slayer3d_game_data_haptics_policy *out_policy);

    /**
     * @brief Resolve the authored network pause input action id.
     *
     * The pause binding is authored under `network.runtime_bindings.pause`.
     * The `action` value references an input action that should request
     * pause/resume in network play.
     *
     * @param runtime Loaded game data runtime.
     * @param out_action_id Receives the runtime input action id.
     * @return true when the pause action binding exists and resolves.
     */
    bool slayer3d_game_data_get_network_runtime_pause_action(const slayer3d_game_data_runtime *runtime,
                                                             int *out_action_id);

    /**
     * @brief Read the authored network pause state property.
     *
     * The pause state is authored under `network.runtime_bindings.pause.state`
     * as an actor reference and bool property name. This lets host code mirror
     * pause state into replicated game state without knowing concrete actor ids
     * or property keys.
     *
     * @param runtime Loaded game data runtime.
     * @param out_paused Receives the current pause state.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pause state binding exists and resolves to a bool.
     */
    bool slayer3d_game_data_get_network_runtime_pause_state(const slayer3d_game_data_runtime *runtime, bool *out_paused,
                                                            char *error_buffer, int error_buffer_size);

    /**
     * @brief Write the authored network pause state property.
     *
     * The pause state is authored under `network.runtime_bindings.pause.state`
     * as an actor reference and bool property name. This helper sets that
     * property to @p paused.
     *
     * @param runtime Loaded game data runtime.
     * @param paused New pause state.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pause state binding exists and was written.
     */
    bool slayer3d_game_data_set_network_runtime_pause_state(slayer3d_game_data_runtime *runtime, bool paused,
                                                            char *error_buffer, int error_buffer_size);

    /**
     * @brief Format a diagnostic summary for an authored network snapshot channel.
     *
     * The helper walks the named host-to-client replication channel in authored
     * schema order and appends every replicated actor field to @p buffer. It
     * also includes the active scene and authored `network.session_flow`
     * scene-state key values when present. This is intended for lightweight
     * host/client diagnostics without hard-coding game actor ids or property
     * paths in the host program.
     *
     * @param runtime Runtime whose current state should be described.
     * @param replication_name Authored host-to-client replication channel name.
     * @param tick Simulation or packet tick to include in the description.
     * @param buffer Destination text buffer.
     * @param buffer_size Destination text buffer size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when @p buffer contains a complete, null-terminated summary.
     */
    bool slayer3d_game_data_describe_network_snapshot(const slayer3d_game_data_runtime *runtime,
                                                      const char *replication_name, Uint32 tick, char *buffer,
                                                      size_t buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Emit an authored network snapshot diagnostic when policy allows.
     *
     * The named policy is read from `network.diagnostics.snapshots[]`. The
     * policy chooses the replicated channel to describe, enabled state, log
     * level, cadence, session-state inclusion, and message template. Template
     * placeholders can reference `{name}`, `{event}`, `{extra}`, `{tick}`, and
     * `{description}`.
     *
     * If the policy is disabled or cadence suppresses the message, the function
     * returns true with @p out_logged set to false.
     *
     * @param runtime Runtime whose current state should be logged.
     * @param diagnostic_name Authored diagnostic policy name.
     * @param tick Simulation or packet tick to include in the description.
     * @param event Optional event label, such as `host_snapshot_sent`.
     * @param extra Optional caller-provided context string.
     * @param out_logged Optional flag set true only when a log line is emitted.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the policy was handled successfully.
     */
    bool slayer3d_game_data_log_network_snapshot_diagnostic(slayer3d_game_data_runtime *runtime,
                                                            const char *diagnostic_name, Uint32 tick, const char *event,
                                                            const char *extra, bool *out_logged, char *error_buffer,
                                                            int error_buffer_size);

    /**
     * @brief Encode an authored host-to-client replication snapshot.
     *
     * The named replication channel must exist in the loaded `network`
     * schema and have `direction: "host_to_client"`. The packet includes a
     * deterministic header, schema hash, channel index, tick, typed field
     * tags, and field values in authored schema order. Callers provide the
     * destination buffer; no allocation is performed.
     *
     * @param runtime Runtime whose actor state should be serialized.
     * @param replication_name Authored replication channel name.
     * @param tick Authoritative simulation tick to include in the snapshot.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the full snapshot was encoded.
     */
    bool slayer3d_game_data_encode_network_snapshot(const slayer3d_game_data_runtime *runtime,
                                                    const char *replication_name, Uint32 tick, void *buffer,
                                                    size_t buffer_size, size_t *out_size, char *error_buffer,
                                                    int error_buffer_size);

    /**
     * @brief Encode a host-to-client snapshot packet by runtime binding semantic.
     *
     * @p binding_name is resolved through `network.runtime_bindings.replication`
     * before encoding. Use this from generic session/runtime code so the loop
     * does not need to know concrete replication channel names.
     */
    bool slayer3d_game_data_encode_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                            const char *binding_name, Uint32 tick, void *buffer,
                                                            size_t buffer_size, size_t *out_size, char *error_buffer,
                                                            int error_buffer_size);

    /**
     * @brief Encode and send a host-to-client snapshot packet by runtime binding.
     */
    bool slayer3d_game_data_send_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_network_session *session, const char *binding_name,
                                                          Uint32 tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply an authored host-to-client replication snapshot.
     *
     * The packet must match the runtime schema hash and reference a
     * host-to-client channel in the loaded `network` schema. Field tags and
     * payload sizes are checked strictly. On failure, the function returns
     * false and reports a best-effort error; callers should discard the
     * packet. Successfully decoded values are applied directly to actors.
     *
     * @param runtime Runtime whose actor state should be updated.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_tick Receives the authoritative snapshot tick, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the snapshot was decoded and applied completely.
     */
    bool slayer3d_game_data_apply_network_snapshot(slayer3d_game_data_runtime *runtime, const void *packet,
                                                   size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                                   int error_buffer_size);

    /**
     * @brief Decode and apply a host-to-client snapshot expected by runtime binding.
     *
     * The packet must be a valid snapshot packet for the concrete replication
     * channel mapped by @p binding_name. This prevents generic session loops
     * from accidentally applying a valid but unexpected channel.
     */
    bool slayer3d_game_data_apply_network_runtime_snapshot(slayer3d_game_data_runtime *runtime,
                                                           const char *binding_name, const void *packet,
                                                           size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Encode an authored client-to-host input replication packet.
     *
     * The named replication channel must exist in the loaded `network`
     * schema and have `direction: "client_to_host"`. Each authored input
     * action is sampled from @p input as a float value and written in schema
     * order with strict field type tags. Callers provide the destination
     * buffer; no allocation is performed.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param replication_name Authored replication channel name.
     * @param input Input manager whose current snapshot should be serialized.
     * @param tick Client simulation/input tick to include in the packet.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the full input packet was encoded.
     */
    bool slayer3d_game_data_encode_network_input(const slayer3d_game_data_runtime *runtime,
                                                 const char *replication_name, const slayer3d_input_manager *input,
                                                 Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode a client-to-host input packet by runtime binding semantic.
     */
    bool slayer3d_game_data_encode_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                         const char *binding_name, const slayer3d_input_manager *input,
                                                         Uint32 tick, void *buffer, size_t buffer_size,
                                                         size_t *out_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode and send a client-to-host input packet by runtime binding.
     */
    bool slayer3d_game_data_send_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_network_session *session, const char *binding_name,
                                                       const slayer3d_input_manager *input, Uint32 tick,
                                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply an authored client-to-host input packet.
     *
     * The packet must match the runtime schema hash and reference a
     * client-to-host input channel in the loaded `network` schema. Decoded
     * action values are applied to @p input as action overrides. On failure,
     * no overrides are changed.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param input Input manager that should receive replicated action overrides.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_tick Receives the client input tick, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded and all action overrides were applied.
     */
    bool slayer3d_game_data_apply_network_input(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_input_manager *input, const void *packet, size_t packet_size,
                                                Uint32 *out_tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply a client-to-host input packet expected by runtime binding.
     *
     * The packet must be a valid input packet for the concrete replication
     * channel mapped by @p binding_name. On failure, no input overrides are
     * changed.
     */
    bool slayer3d_game_data_apply_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                        const char *binding_name, slayer3d_input_manager *input,
                                                        const void *packet, size_t packet_size, Uint32 *out_tick,
                                                        char *error_buffer, int error_buffer_size);

    /**
     * @brief Clear action overrides declared by an authored input replication channel.
     *
     * This is useful when a peer disconnects or a networked scene exits so
     * remote action overrides cannot leak into local play.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param replication_name Authored client-to-host replication channel name.
     * @param input Input manager whose replicated overrides should be cleared.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when all authored input overrides were cleared.
     */
    bool slayer3d_game_data_clear_network_input_overrides(const slayer3d_game_data_runtime *runtime,
                                                          const char *replication_name, slayer3d_input_manager *input,
                                                          char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode an authored network control message packet.
     *
     * The named control message must exist in the loaded `network`
     * `control_messages` array. The packet includes a deterministic header,
     * schema hash, control-message index, and tick. Callers provide the
     * destination buffer; no allocation is performed.
     *
     * @param runtime Runtime containing the authored network schema.
     * @param control_name Authored control message name.
     * @param tick Simulation or wall-clock tick to include in the packet.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the control packet was encoded.
     */
    bool slayer3d_game_data_encode_network_control(const slayer3d_game_data_runtime *runtime, const char *control_name,
                                                   Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                   char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode an authored control packet by runtime binding semantic.
     *
     * @p binding_name is resolved through `network.runtime_bindings.controls`
     * before encoding. Use this from generic session/runtime code so the loop
     * does not need to know concrete control-message names.
     */
    bool slayer3d_game_data_encode_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                           const char *binding_name, Uint32 tick, void *buffer,
                                                           size_t buffer_size, size_t *out_size, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Decode an authored network control message packet.
     *
     * The packet must match the runtime schema hash and reference an authored
     * control message in the loaded `network` schema. On success @p out_control
     * receives runtime-owned descriptor strings and the packet tick.
     *
     * @param runtime Runtime containing the authored network schema.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_control Receives decoded control metadata.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded completely.
     */
    bool slayer3d_game_data_decode_network_control(const slayer3d_game_data_runtime *runtime, const void *packet,
                                                   size_t packet_size, slayer3d_game_data_network_control *out_control,
                                                   char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode an authored control packet and resolve its runtime binding.
     *
     * On success, @p out_binding receives the semantic name from
     * `network.runtime_bindings.controls`, while @p out_control receives the
     * concrete decoded control metadata. Both strings are owned by @p runtime.
     */
    bool slayer3d_game_data_decode_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                           const void *packet, size_t packet_size,
                                                           const char **out_binding,
                                                           slayer3d_game_data_network_control *out_control,
                                                           char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode and send an authored control packet by runtime binding.
     *
     * This helper is transport-light: the caller still owns the session and
     * higher-level flow, but packet naming, encoding, and send error reporting
     * are centralized in the engine.
     */
    bool slayer3d_game_data_send_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_network_session *session, const char *binding_name,
                                                         Uint32 tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and emit an authored network control message signal.
     *
     * This validates the packet with slayer3d_game_data_decode_network_control()
     * and emits the control message's authored signal on the runtime session
     * bus. The signal payload includes `network_control`, `network_direction`,
     * and `network_tick` fields.
     *
     * @param runtime Runtime containing the authored network schema and session bus.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_control Receives decoded control metadata, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded and the signal emitted.
     */
    bool slayer3d_game_data_apply_network_control(slayer3d_game_data_runtime *runtime, const void *packet,
                                                  size_t packet_size, slayer3d_game_data_network_control *out_control,
                                                  char *error_buffer, int error_buffer_size);

    /**
     * @brief Validate a JSON game data file without instantiating runtime state.
     *
     * Validation checks schema, authored names, references, supported generic
     * logic primitives, script manifest structure, dependency cycles, and script
     * file existence. It can emit warnings for suspicious but non-fatal data,
     * such as unused adapters or unsupported component types.
     *
     * @param path JSON file path.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_game_data_validate_file(const char *path, const slayer3d_game_data_validation_options *options,
                                          char *error_buffer, int error_buffer_size);

    /**
     * @brief Validate a JSON game data asset through a resolver.
     *
     * Validation reads the JSON and referenced script files from @p assets, so
     * authored data can be checked the same way whether it comes from a source
     * tree, packed archive, or embedded pack.
     *
     * @param assets Resolver containing the JSON asset and referenced scripts.
     * @param asset_path Virtual path, such as asset://pong.game.json.
     * @param options Optional validation options and diagnostic callback.
     * @param error_buffer Optional buffer for the first fatal diagnostic.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no fatal validation error was found.
     */
    bool slayer3d_game_data_validate_asset(slayer3d_asset_resolver *assets, const char *asset_path,
                                           const slayer3d_game_data_validation_options *options, char *error_buffer,
                                           int error_buffer_size);

    /**
     * @brief Destroy a loaded game data runtime.
     *
     * Disconnects installed signal handlers and frees the parsed document.
     * Session services and registered actors are not destroyed.
     */
    void slayer3d_game_data_destroy(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Register a named native game-specific adapter callback.
     *
     * Re-registering a name replaces the callback and userdata. If the JSON file
     * declared a Lua function for the same adapter, the native callback becomes
     * the active implementation. The adapter name is copied by the runtime.
     */
    bool slayer3d_game_data_register_adapter(slayer3d_game_data_runtime *runtime, const char *name,
                                             slayer3d_game_data_adapter_fn callback, void *userdata);

    /**
     * @brief Reload Lua scripts and rebind Lua adapters atomically.
     *
     * This development-time API reloads the runtime's script manifest through
     * @p assets, resolves all authored Lua adapter functions in a fresh Lua
     * state, and commits the new state only after the full reload succeeds.
     * When a script has a syntax error, returns the wrong type, is missing, or
     * no longer contains a referenced adapter function, the existing scripts and
     * adapter bindings remain active.
     *
     * Native adapters registered with slayer3d_game_data_register_adapter() remain
     * active and are not replaced by reloaded Lua functions.
     *
     * @param runtime Loaded game data runtime.
     * @param assets Resolver containing the updated script assets.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when scripts were reloaded and committed, or when the runtime
     * has no scripts to reload.
     */
    bool slayer3d_game_data_reload_scripts(slayer3d_game_data_runtime *runtime, slayer3d_asset_resolver *assets,
                                           char *error_buffer, int error_buffer_size);

    /**
     * @brief Advance JSON-authored controllers, motion, and sensors by one tick.
     *
     * Call after input is refreshed and before rendering. This updates generic
     * control/motion components, invokes controller adapters, evaluates sensors,
     * and emits any authored signals.
     */
    bool slayer3d_game_data_update(slayer3d_game_data_runtime *runtime, float dt);

    /** @brief Find an authored signal id by name, or -1 when missing. */
    int slayer3d_game_data_find_signal(const slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored input action id by name, or -1 when missing. */
    int slayer3d_game_data_find_action(const slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored actor by name in the runtime's session registry. */
    slayer3d_registered_actor *slayer3d_game_data_find_actor(const slayer3d_game_data_runtime *runtime,
                                                             const char *name);

    /** @brief Find the first authored actor whose entity data contains @p tag. */
    slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tag(const slayer3d_game_data_runtime *runtime,
                                                                      const char *tag);

    /**
     * @brief Find the first authored actor whose entity data contains every tag.
     *
     * Tags are matched against the entity's `tags` array in the loaded JSON
     * document. This lets game code request roles like `{"paddle", "player"}`
     * without depending on exact entity names.
     */
    slayer3d_registered_actor *slayer3d_game_data_find_actor_with_tags(const slayer3d_game_data_runtime *runtime,
                                                                       const char *const *tags, int tag_count);

    /**
     * @brief Read data-authored application lifecycle hooks.
     *
     * Missing fields return neutral values: signal/action ids are -1 and
     * transition names are NULL.
     */
    bool slayer3d_game_data_get_app_control(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_app_control *out_control);

    /**
     * @brief Return whether an authored signal should apply live window settings.
     *
     * Games declare these signals under `app.window.apply_signal` or
     * `app.window.apply_signals`. This lets reusable menu controls apply display
     * mode, renderer, and V-sync changes immediately without hard-coding menu
     * names in the host.
     */
    bool slayer3d_game_data_app_signal_applies_window_settings(const slayer3d_game_data_runtime *runtime,
                                                               int signal_id);

    /**
     * @brief Evaluate the data-authored app pause condition.
     *
     * Returns true when `app.pause.allowed_if` is absent. When present, the
     * condition uses the same generic condition language as UI visibility:
     * actor property comparisons, app pause checks, camera checks, and
     * all/any/not composition. @p metrics may be NULL when the condition does
     * not refer to app metrics.
     */
    bool slayer3d_game_data_app_pause_allowed(const slayer3d_game_data_runtime *runtime,
                                              const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Advance data-authored presentation clocks.
     *
     * Presentation clocks are generic data-driven oscillators/counters used by
     * UI, lights, and other render-facing effects. Authored clocks may write
     * into actor properties so scripts, UI bindings, and render evaluation can
     * share the same source of truth.
     */
    bool slayer3d_game_data_update_presentation_clocks(slayer3d_game_data_runtime *runtime, float dt, bool paused,
                                                       bool pause_entered);

    /**
     * @brief Advance the active scene's authored input-activity controller.
     *
     * Scenes may author an `activity` object to drive reusable attract-mode,
     * kiosk, title-screen, or cutscene overlays. The controller tracks the
     * active scene, detects input activity, emits `on_enter`, `on_idle`,
     * `on_active`, and `periodic` action lists, and keeps behavior in game
     * data instead of host glue.
     *
     * Supported activity input modes are `any`, `action`, and `disabled`.
     * Periodic entries may reset idle time so data can temporarily reveal UI
     * prompts before allowing them to fade away again.
     *
     * @param runtime Loaded game data runtime.
     * @param input Current input manager, or NULL when input activity should be ignored.
     * @param dt Delta time in seconds.
     * @return true when authored activity actions completed successfully.
     */
    bool slayer3d_game_data_update_scene_activity(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_input_manager *input, float dt);

    /**
     * @brief Return whether the active scene activity should consume wake input.
     *
     * This is a query-only helper for app-flow/menu controllers. When a scene
     * has entered its authored idle state and matching input is pressed, data
     * may request that the current input be used only to wake the scene's
     * activity controller. The next scene-activity update will run `on_active`.
     *
     * @param runtime Loaded game data runtime.
     * @param input Current input manager.
     * @param out_block_menus Optional output set when menu input should be blocked.
     * @param out_block_scene_shortcuts Optional output set when scene shortcuts should be blocked.
     * @return true when the matching wake input should be consumed for this frame.
     */
    bool slayer3d_game_data_scene_activity_consumes_wake_input(const slayer3d_game_data_runtime *runtime,
                                                               const slayer3d_input_manager *input,
                                                               bool *out_block_menus, bool *out_block_scene_shortcuts);

    /**
     * @brief Read the authored UI pulse phase.
     *
     * Returns @p fallback when no `presentation.ui_pulse_clock` is authored or
     * the named clock has no current value.
     */
    float slayer3d_game_data_ui_pulse_phase(const slayer3d_game_data_runtime *runtime, float fallback);

    /**
     * @brief Read authored FPS metric sample duration in seconds.
     */
    float slayer3d_game_data_fps_sample_seconds(const slayer3d_game_data_runtime *runtime, float fallback);

    /** @brief Read a font asset descriptor by id from `assets.fonts`. */
    bool slayer3d_game_data_get_font_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                           slayer3d_game_data_font_asset *out_font);

    /** @brief Read an image asset descriptor by id from `assets.images`. */
    bool slayer3d_game_data_get_image_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_image_asset *out_image);

    /** @brief Read a 3D model asset descriptor by id from `assets.models`. */
    bool slayer3d_game_data_get_model_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_model_asset *out_model);

    /** @brief Read a sound-effect asset descriptor by id from `assets.sounds`. */
    bool slayer3d_game_data_get_sound_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_sound_asset *out_sound);

    /** @brief Read a music asset descriptor by id from `assets.music`. */
    bool slayer3d_game_data_get_music_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                            slayer3d_game_data_music_asset *out_music);

    /** @brief Read an ambient-zone asset descriptor by id from `assets.ambient`. */
    bool slayer3d_game_data_get_ambient_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_game_data_ambient_asset *out_ambient);

    /** @brief Read a sprite asset descriptor by id from `assets.sprites`. */
    bool slayer3d_game_data_get_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                             slayer3d_game_data_sprite_asset *out_sprite);

    /**
     * @brief Load a sprite asset by id from `assets.sprites`.
     *
     * The runtime looks up the authored sprite descriptor, resolves the
     * source image through the game's asset resolver, and builds billboard
     * textures plus rotation sets ready for sprite actors.
     */
    bool slayer3d_game_data_load_sprite_asset(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_sprite_asset_runtime *out_sprite, char *error_buffer,
                                              int error_buffer_size);

    /**
     * @brief Resolve an authored audio path to a filesystem path usable by audio backends.
     *
     * Resolver-backed assets such as `asset://audio/title.ogg` are materialized
     * into the runtime's `cache://audio` storage root. Plain filesystem paths
     * are resolved relative to the loaded game data file and returned without
     * copying. The returned path is copied into @p out_path and remains valid
     * independently of the runtime.
     *
     * @param runtime Loaded game data runtime.
     * @param path Authored audio path or asset URI.
     * @param out_path Buffer that receives the filesystem path.
     * @param out_path_size Size of @p out_path in bytes.
     * @return true when the path was resolved and copied.
     */
    bool slayer3d_game_data_prepare_audio_file(slayer3d_game_data_runtime *runtime, const char *path, char *out_path,
                                               int out_path_size);

    /**
     * @brief Return the currently active authored camera name.
     *
     * The returned pointer is owned by the parsed JSON document and remains
     * valid until the runtime is destroyed.
     */
    const char *slayer3d_game_data_active_camera(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored non-adapter camera by name.
     *
     * Adapter cameras are game-specific and return false here because their
     * final pose is computed by game code or script. Orthographic cameras use
     * `size` as slayer3d_camera3d::fovy; perspective cameras use `fov` or the
     * legacy `fovy` field with an optional `fov_axis`.
     */
    bool slayer3d_game_data_get_camera(const slayer3d_game_data_runtime *runtime, const char *name,
                                       slayer3d_camera3d *out_camera);

    /**
     * @brief Read a numeric custom property from an authored camera.
     *
     * This lets games keep camera tuning data in JSON even when the camera pose
     * itself is adapter-driven.
     */
    bool slayer3d_game_data_get_camera_float(const slayer3d_game_data_runtime *runtime, const char *camera_name,
                                             const char *property_name, float *out_value);

    /**
     * @brief Read the authored world unit convention.
     *
     * Games may omit these fields; SLAYER3D then reports the engine default:
     * units="meters" and meters_per_unit=1.0. This is metadata for tools,
     * editors, physics tuning, and documentation. The renderer interprets all
     * authored positions consistently as world units.
     */
    bool slayer3d_game_data_get_world_units(const slayer3d_game_data_runtime *runtime, const char **out_units,
                                            float *out_meters_per_unit);

    /** @brief Return the number of authored world lights. */
    int slayer3d_game_data_world_light_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the authored world ambient light color.
     *
     * Values are linear RGB in the same range expected by
     * slayer3d_set_ambient_light().
     */
    bool slayer3d_game_data_get_world_ambient_light(const slayer3d_game_data_runtime *runtime, float out_rgb[3]);

    /**
     * @brief Read an authored world light by zero-based index.
     *
     * The returned light is suitable for passing to slayer3d_add_light(). Lights
     * may target one entity with `target_entity`, or the first active-scene
     * entity in an ordered `target_entities` fallback list.
     */
    bool slayer3d_game_data_get_world_light(const slayer3d_game_data_runtime *runtime, int index,
                                            slayer3d_light *out_light);

    /**
     * @brief Read an authored world light with generic visual effects evaluated.
     *
     * Supported light effects include `pulse`, `color_cycle`, and `flash`,
     * allowing data to drive color blends, intensity changes, and range changes
     * over time or from actor properties. Passing NULL for @p eval uses a zeroed
     * evaluation context.
     */
    bool slayer3d_game_data_get_world_light_evaluated(const slayer3d_game_data_runtime *runtime, int index,
                                                      const slayer3d_game_data_render_eval *eval,
                                                      slayer3d_light *out_light);

    /**
     * @brief Iterate active authored render primitive components.
     *
     * Components currently supported by this iterator include `render.cube`,
     * `render.sphere`, `render.mesh_primitive`, `render.composite`,
     * `render.sprite`, and `render.model`. Iteration skips inactive actors.
     */
    bool slayer3d_game_data_for_each_render_primitive(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_render_primitive_fn callback, void *userdata);

    /**
     * @brief Iterate active authored render primitives with dynamic effects evaluated.
     *
     * This applies generic `effects` authored on render primitive components,
     * such as property-driven flash colors, size offsets, and time-driven
     * pulses. Passing NULL for @p eval uses a zeroed evaluation context.
     */
    bool slayer3d_game_data_for_each_render_primitive_evaluated(const slayer3d_game_data_runtime *runtime,
                                                                const slayer3d_game_data_render_eval *eval,
                                                                slayer3d_game_data_render_primitive_fn callback,
                                                                void *userdata);

    /**
     * @brief Read an authored particle emitter component from an entity.
     *
     * The returned config is ready for slayer3d_create_particle_emitter(). Texture
     * references are intentionally not resolved here yet, so config.texture is
     * always NULL.
     */
    bool slayer3d_game_data_get_particle_emitter(const slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                 slayer3d_particle_config *out_config);

    /**
     * @brief Read optional draw-time emissive color for a particle emitter entity.
     *
     * The color is read from the emitter component's `draw_emissive` field and
     * defaults to zero when not authored.
     */
    bool slayer3d_game_data_get_particle_emitter_draw_emissive(const slayer3d_game_data_runtime *runtime,
                                                               const char *entity_name, slayer3d_vec3 *out_rgb);

    /**
     * @brief Iterate active authored particle emitter components.
     *
     * Iteration skips inactive actors and entities not included by the active
     * scene. The descriptor's config is ready for slayer3d_create_particle_emitter().
     */
    bool slayer3d_game_data_for_each_particle_emitter(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_particle_emitter_fn callback, void *userdata);

    /**
     * @brief Read authored render setup.
     *
     * Missing fields produce conservative defaults: black clear color, lighting
     * enabled, bloom/SSAO enabled, and ACES tonemapping.
     */
    bool slayer3d_game_data_get_render_settings(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_game_data_render_settings *out_settings);

    /**
     * @brief Read a named authored transition descriptor.
     *
     * @p name is looked up under the top-level `transitions` object.
     */
    bool slayer3d_game_data_get_transition(const slayer3d_game_data_runtime *runtime, const char *name,
                                           slayer3d_game_data_transition_desc *out_transition);

    /**
     * @brief Return the active scene name.
     *
     * Scene names come from the top-level `scenes.initial` field and referenced
     * scene files. Returns NULL when the game does not author scenes.
     */
    const char *slayer3d_game_data_active_scene(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return the number of authored scenes loaded by the runtime.
     *
     * Games without a `scenes.files` manifest return 0. Scene order matches
     * the authored manifest order and is stable for the lifetime of the
     * runtime.
     */
    int slayer3d_game_data_scene_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return an authored scene name by manifest index.
     *
     * @param index Zero-based scene index in the loaded manifest.
     * @return Runtime-owned scene name, or NULL when @p index is out of range.
     */
    const char *slayer3d_game_data_scene_name_at(const slayer3d_game_data_runtime *runtime, int index);

    /**
     * @brief Switch to an authored scene by name.
     *
     * The new scene's `on_enter_signal`, when present, is emitted after the
     * active scene changes. The enter payload always includes `from_scene` and
     * `to_scene` string keys. Returns false when @p scene_name is unknown.
     */
    bool slayer3d_game_data_set_active_scene(slayer3d_game_data_runtime *runtime, const char *scene_name);

    /**
     * @brief Switch to an authored scene and pass state to its enter signal.
     *
     * @p payload is copied into a transient enter payload and forwarded to the
     * target scene's `on_enter_signal`; the caller keeps ownership of @p
     * payload. The runtime also writes `from_scene` and `to_scene`, overriding
     * same-named keys in @p payload so every scene-enter observer receives
     * reliable transition context.
     *
     * Use slayer3d_game_data_mutable_scene_state() for data that must persist
     * after enter-signal processing.
     */
    bool slayer3d_game_data_set_active_scene_with_payload(slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                          const slayer3d_properties *payload);

    /**
     * @brief Return whether the active scene should advance gameplay systems.
     *
     * Scenes default to updating gameplay when they do not specify
     * `updates_game`. Games without authored scenes return true.
     */
    bool slayer3d_game_data_active_scene_updates_game(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Return whether an authored update phase should run for the active scene.
     *
     * @p phase is an authored phase name such as `simulation`,
     * `property_effects`, `particles`, or `presentation`. Scene-level
     * `update_phases` entries override top-level entries. Missing phases use
     * conservative defaults: simulation follows `updates_game` and does not run
     * while paused; presentation/property effects/particles run in both paused
     * and unpaused frames.
     */
    bool slayer3d_game_data_active_scene_update_phase(const slayer3d_game_data_runtime *runtime, const char *phase,
                                                      bool paused);

    /**
     * @brief Return whether the active scene should render the authored world.
     *
     * Scenes default to rendering the world when they do not specify
     * `renders_world`. Games without authored scenes return true.
     */
    bool slayer3d_game_data_active_scene_renders_world(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read the active scene's optional skybox descriptor.
     *
     * Scene skyboxes are authored under `world.skybox` and reference image
     * assets by id. Returns false when the active scene has no skybox.
     */
    bool slayer3d_game_data_get_active_scene_skybox(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_scene_skybox *out_skybox);

    /**
     * @brief Return whether an entity belongs to the active scene.
     *
     * Scenes that omit an `entities` list include all loaded entities for
     * backward compatibility. Scenes with an empty list include no entities.
     */
    bool slayer3d_game_data_active_scene_has_entity(const slayer3d_game_data_runtime *runtime, const char *entity_name);

    /**
     * @brief Return whether the active scene allows an input action.
     *
     * If a scene omits `input.actions`, all actions are allowed. When present,
     * the action must be listed there or in top-level
     * `app.input_policy.global_actions`.
     */
    bool slayer3d_game_data_active_scene_allows_action(const slayer3d_game_data_runtime *runtime, int action_id);

    /**
     * @brief Return whether the active scene requests relative mouse capture.
     *
     * Scenes may author `input.mouse_capture` as `never`, `unpaused`, or
     * `always`, plus an optional `input.mouse_capture_if` condition. Missing
     * policy defaults to `never`. The @p paused argument lets generic hosts
     * release the cursor while an authored pause/menu overlay is active.
     */
    bool slayer3d_game_data_active_scene_mouse_capture(const slayer3d_game_data_runtime *runtime, bool paused);

    /**
     * @brief Read authored scene transition policy.
     *
     * Missing fields use stable defaults: same-scene requests and interrupting
     * active transitions are rejected, and accepted scene requests reset menu
     * input arming.
     */
    bool slayer3d_game_data_get_scene_transition_policy(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_scene_transition_policy *out_policy);

    /**
     * @brief Read the transition descriptor attached to a scene phase.
     *
     * @p phase is commonly `enter` or `exit` and is looked up under the scene's
     * `transitions` object. Returns false when the scene or phase is missing.
     */
    bool slayer3d_game_data_get_scene_transition(const slayer3d_game_data_runtime *runtime, const char *scene_name,
                                                 const char *phase, slayer3d_game_data_transition_desc *out_transition);

    /**
     * @brief Read the active scene's primary menu, if any.
     *
     * The first menu whose optional `active_if` condition passes is considered
     * active. Conditions that depend on frame metrics, such as `app.paused`,
     * evaluate as false through this convenience wrapper.
     */
    bool slayer3d_game_data_get_active_menu(const slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_menu *out_menu);

    /**
     * @brief Read the active scene menu using current frame metrics.
     *
     * This variant lets authored menu `active_if` conditions depend on app
     * pause state, camera state, actor properties, or other metrics-backed UI
     * conditions. It returns false when no active scene menu is eligible.
     */
    bool slayer3d_game_data_get_active_menu_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                        const slayer3d_game_data_ui_metrics *metrics,
                                                        slayer3d_game_data_menu *out_menu);

    /**
     * @brief Move a menu selection by @p delta with wrap-around.
     *
     * Positive values move down, negative values move up. Returns false when
     * the menu is unknown or contains no items.
     */
    bool slayer3d_game_data_menu_move(slayer3d_game_data_runtime *runtime, const char *menu_name, int delta);

    /**
     * @brief Publish side effects for the currently selected menu item without moving selection.
     *
     * Dynamic-list rows may author selected-index or selected-value scene-state
     * outputs. This helper refreshes those outputs for the current highlighted
     * item. It does not emit signals, select the item, or change scenes.
     */
    bool slayer3d_game_data_publish_menu_selection(slayer3d_game_data_runtime *runtime, const char *menu_name);

    /**
     * @brief Remove every row from a runtime collection.
     *
     * Runtime collections are named, host-populated row sets that authored UI
     * can read through dynamic-list menu sources. Clearing an unknown
     * collection is a successful no-op.
     */
    bool slayer3d_game_data_runtime_collection_clear(slayer3d_game_data_runtime *runtime, const char *collection_name);

    /**
     * @brief Return the number of rows currently published in a runtime collection.
     *
     * Unknown collections have a count of zero.
     */
    int slayer3d_game_data_runtime_collection_count(const slayer3d_game_data_runtime *runtime,
                                                    const char *collection_name);

    /**
     * @brief Publish a string field on one runtime collection row.
     *
     * Rows are zero-based and created on demand. Host systems should publish
     * contiguous rows and then clear the collection before republishing a
     * shorter result set.
     */
    bool slayer3d_game_data_runtime_collection_set_string(slayer3d_game_data_runtime *runtime,
                                                          const char *collection_name, int row_index,
                                                          const char *field_name, const char *value);

    /** @brief Publish an integer field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_int(slayer3d_game_data_runtime *runtime, const char *collection_name,
                                                       int row_index, const char *field_name, int value);

    /** @brief Publish a floating-point field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_float(slayer3d_game_data_runtime *runtime,
                                                         const char *collection_name, int row_index,
                                                         const char *field_name, float value);

    /** @brief Publish a boolean field on one runtime collection row. */
    bool slayer3d_game_data_runtime_collection_set_bool(slayer3d_game_data_runtime *runtime,
                                                        const char *collection_name, int row_index,
                                                        const char *field_name, bool value);

    /**
     * @brief Start or replace a named runtime-owned UDP direct-connect client session.
     *
     * The session is owned by @p runtime and remains valid until canceled,
     * replaced, or the runtime is destroyed. @p status_key, @p state_key, and
     * @p connected_key are optional scene-state keys updated after creation.
     */
    bool slayer3d_game_data_network_direct_connect_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                         const char *host, int port, const char *status_key,
                                                         const char *state_key, const char *connected_key);

    /**
     * @brief Cancel and destroy a named runtime-owned direct-connect session.
     *
     * Canceling an unknown session is a successful no-op.
     */
    bool slayer3d_game_data_network_direct_connect_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                          const char *status_key, const char *state_key,
                                                          const char *connected_key, const char *status);

    /**
     * @brief Publish a named runtime-owned direct-connect session's status into scene state.
     */
    bool slayer3d_game_data_network_direct_connect_publish_status(slayer3d_game_data_runtime *runtime,
                                                                  const char *session_name, const char *status_key,
                                                                  const char *state_key, const char *connected_key);

    /**
     * @brief Return a runtime-owned direct-connect session, or NULL when absent.
     *
     * The caller must not destroy the returned pointer.
     */
    slayer3d_network_session *slayer3d_game_data_get_network_direct_connect_session(slayer3d_game_data_runtime *runtime,
                                                                                    const char *session_name);

    /**
     * @brief Start or keep a named runtime-owned UDP host session alive.
     *
     * Host sessions listen for exactly one client. The session is owned by
     * @p runtime and remains valid until canceled, replaced, or the runtime is
     * destroyed. Optional scene-state keys publish human-readable status,
     * endpoint, peer label, and connected state.
     */
    bool slayer3d_game_data_network_host_start(slayer3d_game_data_runtime *runtime, const char *session_name, int port,
                                               const char *advertised_name, const char *status_key,
                                               const char *endpoint_key, const char *peer_key,
                                               const char *connected_key);

    /**
     * @brief Cancel and destroy a named runtime-owned host session.
     *
     * Canceling an unknown host session is a successful no-op.
     */
    bool slayer3d_game_data_network_host_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                const char *status_key, const char *endpoint_key, const char *peer_key,
                                                const char *connected_key, const char *status);

    /**
     * @brief Publish a named runtime-owned host session's status into scene state.
     */
    bool slayer3d_game_data_network_host_publish_status(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                        const char *status_key, const char *endpoint_key,
                                                        const char *peer_key, const char *connected_key);

    /**
     * @brief Return a runtime-owned host session, or NULL when absent.
     *
     * The caller must not destroy the returned pointer.
     */
    slayer3d_network_session *slayer3d_game_data_get_network_host_session(slayer3d_game_data_runtime *runtime,
                                                                          const char *session_name);

    /**
     * @brief Start or refresh a named runtime-owned LAN discovery scanner.
     *
     * Results are published to @p collection_name when provided. Each row
     * contains `label`, `name`, `host`, `port`, `status`, and `endpoint`
     * fields. @p status_key and @p count_key are optional scene-state outputs.
     */
    bool slayer3d_game_data_network_discovery_start(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                    const char *host, int port, int local_port,
                                                    const char *collection_name, const char *status_key,
                                                    const char *count_key);

    /**
     * @brief Advance a named discovery scanner and republish results.
     */
    bool slayer3d_game_data_network_discovery_update(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                     float dt, const char *collection_name, const char *status_key,
                                                     const char *count_key);

    /**
     * @brief Cancel and destroy a named discovery scanner.
     *
     * Canceling an unknown scanner is a successful no-op.
     */
    bool slayer3d_game_data_network_discovery_cancel(slayer3d_game_data_runtime *runtime, const char *session_name,
                                                     const char *collection_name, const char *status_key,
                                                     const char *count_key, const char *status);

    /**
     * @brief Connect a direct-connect session to one row from a discovery collection.
     *
     * The selected collection row must contain `host` and `port` fields.
     */
    bool slayer3d_game_data_network_discovery_connect_selected(
        slayer3d_game_data_runtime *runtime, const char *discovery_name, const char *collection_name,
        int selected_index, const char *direct_connect_name, const char *host_key, const char *port_key,
        const char *status_key, const char *state_key, const char *connected_key, const char *connecting_status);

    /**
     * @brief Read one item from an authored menu.
     *
     * @p index is zero based. Static returned strings remain owned by the
     * runtime. Dynamic-list label/value strings are copied into storage fields
     * on @p out_item and remain valid until @p out_item is overwritten.
     */
    bool slayer3d_game_data_get_menu_item(const slayer3d_game_data_runtime *runtime, const char *menu_name, int index,
                                          slayer3d_game_data_menu_item *out_item);

    /**
     * @brief Apply the generic control behavior authored on a menu item.
     *
     * Toggle controls flip boolean properties, choice controls advance by one
     * authored choice, and range controls increase by one authored step.
     * Returns false when @p item is not a control or its target cannot be
     * resolved.
     */
    bool slayer3d_game_data_apply_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_menu_item *item);

    /**
     * @brief Adjust the generic control behavior authored on a menu item.
     *
     * @p direction should be positive to increase/advance or negative to
     * decrease/rewind. Choice controls wrap across authored choices; range
     * controls clamp to their authored min/max and preserve integer properties
     * when authored with `value_type: "int"`. Toggle controls ignore direction
     * and flip the current boolean value.
     *
     * @return true when a control value changed.
     */
    bool slayer3d_game_data_adjust_menu_item_control(slayer3d_game_data_runtime *runtime,
                                                     const slayer3d_game_data_menu_item *item, int direction);

    /**
     * @brief Start capture mode for an input-binding menu item.
     *
     * The menu item must author a `control` with `type: "input_binding"`.
     * While capture is active, callers should pass input snapshots to
     * slayer3d_game_data_update_menu_input_binding_capture() before normal menu
     * navigation.
     */
    bool slayer3d_game_data_start_menu_input_binding_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                             int item_index);

    /** @brief Return true while a binding menu item is waiting for an input. */
    bool slayer3d_game_data_menu_input_binding_capture_active(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Advance active binding capture from current input.
     *
     * The runtime reads the first keyboard scancode or gamepad button captured
     * by slayer3d_input_update(), depending on the menu item's authored device.
     * Successful captures immediately update the live input manager for every
     * action authored by the menu item.
     */
    slayer3d_game_data_input_binding_capture_status slayer3d_game_data_update_menu_input_binding_capture(
        slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input);

    /**
     * @brief Reset all binding controls in a menu to their authored defaults.
     */
    bool slayer3d_game_data_reset_menu_input_bindings(slayer3d_game_data_runtime *runtime, const char *menu_name);

    /**
     * @brief Start text capture for an authored menu text control.
     *
     * The menu item must author a `control` with `type: "text"`. While active,
     * callers should call slayer3d_game_data_update_menu_text_entry_capture()
     * before normal menu navigation so editing keys are consumed locally.
     */
    bool slayer3d_game_data_start_menu_text_entry_capture(slayer3d_game_data_runtime *runtime, const char *menu_name,
                                                          int item_index);

    /** @brief Return true while a menu text-entry capture is active. */
    bool slayer3d_game_data_menu_text_entry_capture_active(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Advance active text-entry capture from current input.
     *
     * SDL text-input payloads append to the bound string. Backspace and Delete
     * remove the previous UTF-8 codepoint. The menu's select action or Return
     * submits; the menu's back action or the authored cancel key cancels and
     * restores the original value.
     */
    slayer3d_game_data_text_entry_capture_status slayer3d_game_data_update_menu_text_entry_capture(
        slayer3d_game_data_runtime *runtime, const slayer3d_input_manager *input);

    /**
     * @brief Device-count state for active input profile refresh.
     *
     * Initialize with slayer3d_game_data_input_profile_refresh_state_init() before
     * the first refresh. Callers may reset the state when entering a new scene
     * if they want to force one active-profile application on the next frame.
     */
    typedef struct slayer3d_game_data_input_profile_refresh_state
    {
        /** @brief Last observed gamepad count. */
        int gamepad_count;
        /** @brief Whether gamepad_count has been sampled at least once. */
        bool initialized;
    } slayer3d_game_data_input_profile_refresh_state;

    /**
     * @brief Initialize active input profile refresh state.
     */
    void slayer3d_game_data_input_profile_refresh_state_init(slayer3d_game_data_input_profile_refresh_state *state);

    /**
     * @brief Apply one authored input profile to an input manager.
     *
     * Profiles are authored under `input.profiles`. Applying a profile first
     * unbinds every action listed in its `unbind` array, then applies either
     * raw keyboard, mouse, or gamepad bindings or reusable
     * `input.device_assignment_sets` assignments.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to mutate.
     * @param profile_name Authored profile name.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the profile exists and all authored bindings were applied.
     */
    bool slayer3d_game_data_apply_input_profile(slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
                                                const char *profile_name, char *error_buffer, int error_buffer_size);

    /**
     * @brief Apply the first input profile whose authored conditions match.
     *
     * Profiles are evaluated in authored order. `active_if` uses the same
     * condition language as scene UI/menu rules, and optional `min_gamepads`
     * / `max_gamepads` gates use the current input manager device count.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to mutate.
     * @param out_profile_name Receives the applied runtime-owned profile name, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when a matching profile was found and applied.
     */
    bool slayer3d_game_data_apply_active_input_profile(slayer3d_game_data_runtime *runtime,
                                                       slayer3d_input_manager *input, const char **out_profile_name,
                                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Return the first input profile whose authored conditions match.
     *
     * This is a side-effect-free query for hosts and generic runtimes that
     * need to know whether automatic input-profile refresh is currently
     * applicable. It uses the same authored-order, `active_if`, and gamepad
     * gate rules as @ref slayer3d_game_data_apply_active_input_profile.
     *
     * @param runtime Runtime containing authored profile data.
     * @param input Input manager used for live gamepad-count gates.
     * @param out_profile_name Receives the matching runtime-owned profile name, if non-NULL.
     * @return true when a profile currently matches.
     */
    bool slayer3d_game_data_get_active_input_profile_name(const slayer3d_game_data_runtime *runtime,
                                                          const slayer3d_input_manager *input,
                                                          const char **out_profile_name);

    /**
     * @brief Apply the active input profile when connected gamepad count changes.
     *
     * This helper centralizes the common hotplug policy for data-authored input
     * profiles. It applies the active profile on first use, then applies again
     * only when slayer3d_input_gamepad_count() changes. Scene changes that should
     * always rebind controls should still use authored `input.apply_active_profile`
     * actions on scene entry.
     *
     * @param runtime Runtime containing authored profile data and action ids.
     * @param input Input manager to inspect and mutate.
     * @param state Persistent refresh state owned by the caller.
     * @param out_profile_name Receives the applied runtime-owned profile name, if non-NULL and applied.
     * @param out_applied Receives whether a profile was applied this call, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when no refresh was needed or the matching profile was applied.
     */
    bool slayer3d_game_data_apply_active_input_profile_on_device_change(
        slayer3d_game_data_runtime *runtime, slayer3d_input_manager *input,
        slayer3d_game_data_input_profile_refresh_state *state, const char **out_profile_name, bool *out_applied,
        char *error_buffer, int error_buffer_size);

    /**
     * @brief Return the number of scene shortcuts authored under `app.scene_shortcuts`.
     */
    int slayer3d_game_data_scene_shortcut_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored scene shortcut by index.
     *
     * Invalid or unresolved shortcut entries still return their authored names,
     * but use action id -1. This lets validators and hosts report useful
     * diagnostics without failing at runtime.
     */
    bool slayer3d_game_data_scene_shortcut_at(const slayer3d_game_data_runtime *runtime, int index,
                                              slayer3d_game_data_scene_shortcut *out_shortcut);

    /**
     * @brief Return whether the active menu has no held navigation actions.
     *
     * This lets hosts arm menu input after scene entry. Waiting for idle input
     * prevents a key or gamepad button held while launching or switching scenes
     * from immediately activating the new scene's default menu item.
     *
     * Scenes without an active menu return true. A NULL input manager returns
     * false when a menu exists because the runtime cannot prove the menu is idle.
     */
    bool slayer3d_game_data_active_menu_input_is_idle(const slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_input_manager *input);

    /**
     * @brief Iterate authored UI text descriptors.
     *
     * This is equivalent to slayer3d_game_data_for_each_ui_text_for_metrics()
     * with a NULL metrics pointer. Use the metrics-aware iterator when menu
     * presenters or authored visibility conditions depend on frame state such
     * as pause, FPS, or active scene transition state.
     */
    bool slayer3d_game_data_for_each_ui_text(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI text descriptors using current frame metrics.
     *
     * Iteration includes global `ui.text`, active-scene `ui.text`, and menu
     * presenters from global and active-scene `ui.menus`. Conditions on
     * generated menu presenters are evaluated with `metrics`, so app-state
     * dependent menus such as pause overlays can be rendered correctly.
     */
    bool slayer3d_game_data_for_each_ui_text_for_metrics(const slayer3d_game_data_runtime *runtime,
                                                         const slayer3d_game_data_ui_metrics *metrics,
                                                         slayer3d_game_data_ui_text_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI images visible to the active scene.
     *
     * Iteration includes global `ui.images` followed by active-scene
     * `ui.images`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_image_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_image(const slayer3d_game_data_runtime *runtime,
                                              slayer3d_game_data_ui_image_fn callback, void *userdata);

    /**
     * @brief Iterate authored UI rectangles visible to the active scene.
     *
     * Iteration includes global `ui.rects` followed by active-scene
     * `ui.rects`. Visibility is evaluated separately by
     * slayer3d_game_data_ui_rect_is_visible().
     */
    bool slayer3d_game_data_for_each_ui_rect(const slayer3d_game_data_runtime *runtime,
                                             slayer3d_game_data_ui_rect_fn callback, void *userdata);

    /**
     * @brief Initialize runtime UI state to identity values.
     *
     * The initialized state has no override flags, zero offset, scale 1, alpha
     * 1, and white tint.
     */
    void slayer3d_game_data_ui_state_init(slayer3d_game_data_ui_state *state);

    /**
     * @brief Store runtime state for a named authored UI item.
     *
     * The runtime copies @p state and owns the name key internally. State
     * remains active until replaced, cleared by name, or all UI state is
     * cleared.
     */
    bool slayer3d_game_data_set_ui_state(slayer3d_game_data_runtime *runtime, const char *name,
                                         const slayer3d_game_data_ui_state *state);

    /**
     * @brief Read runtime state for a named authored UI item.
     *
     * Returns false when no state exists for @p name. @p out_state is
     * initialized to identity values before lookup.
     */
    bool slayer3d_game_data_get_ui_state(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_ui_state *out_state);

    /** @brief Clear runtime state for one named UI item. */
    bool slayer3d_game_data_clear_ui_state(slayer3d_game_data_runtime *runtime, const char *name);

    /** @brief Clear all runtime UI item state. */
    void slayer3d_game_data_clear_ui_states(slayer3d_game_data_runtime *runtime);

    /**
     * @brief Resolve authored text plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_text may alias @p text.
     */
    bool slayer3d_game_data_resolve_ui_text(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_text *text,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_text *out_text, bool *out_visible);

    /**
     * @brief Resolve authored image plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_image may alias @p image.
     */
    bool slayer3d_game_data_resolve_ui_image(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_ui_image *image,
                                             const slayer3d_game_data_ui_metrics *metrics,
                                             slayer3d_game_data_ui_image *out_image, bool *out_visible);

    /**
     * @brief Resolve authored rectangle plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions,
     * property-driven alpha, and runtime overrides. @p out_rect may alias
     * @p rect.
     */
    bool slayer3d_game_data_resolve_ui_rect(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_rect *rect,
                                            const slayer3d_game_data_ui_metrics *metrics,
                                            slayer3d_game_data_ui_rect *out_rect, bool *out_visible);

    /**
     * @brief Evaluate a UI text descriptor's authored visibility condition.
     *
     * Supports camera-active checks, app pause checks, actor property
     * comparisons, and boolean all/any/not composition. Descriptors without a
     * condition are visible.
     */
    bool slayer3d_game_data_ui_text_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_text *text,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /** @brief Evaluate a UI image's authored `visible_if` condition. */
    bool slayer3d_game_data_ui_image_is_visible(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_ui_image *image,
                                                const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Evaluate a UI rectangle descriptor's authored visibility condition.
     */
    bool slayer3d_game_data_ui_rect_is_visible(const slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_ui_rect *rect,
                                               const slayer3d_game_data_ui_metrics *metrics);

    /**
     * @brief Read the active scene's authored skip policy.
     *
     * The runtime first checks `scene.skip_policy`, then
     * `scene.timeline.skip_policy`. Returns false when no enabled policy is
     * authored for the active scene. Missing fields use conservative defaults:
     * any input, transition preservation enabled, and input consumption enabled.
     */
    bool slayer3d_game_data_get_active_skip_policy(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_game_data_skip_policy *out_policy);

    /**
     * @brief Read the active scene's authored timeline interaction policy.
     *
     * Returns false when the active scene has no autoplaying `timeline` object.
     * Missing policy fields default to false so timelines remain interactive
     * unless the scene author explicitly blocks menus or scene shortcuts.
     */
    bool slayer3d_game_data_get_active_timeline_policy(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_timeline_policy *out_policy);

    /**
     * @brief Initialize reusable timeline state.
     *
     * Safe to call with NULL.
     */
    void slayer3d_game_data_timeline_state_init(slayer3d_game_data_timeline_state *state);

    /**
     * @brief Advance the active scene's authored autoplay timeline.
     *
     * Timeline events are one-shot and must be authored in non-decreasing time
     * order. This helper executes generic actions that are safe to apply inside
     * the data runtime (`signal.emit`, `property.set`, etc.). `scene.request`
     * is reported in @p out_result so hosts can route the request through their
     * own transition flow instead of forcing an immediate scene switch.
     *
     * @param runtime Loaded game data runtime.
     * @param state Persistent timeline state owned by the host.
     * @param dt Delta time in seconds.
     * @param out_result Optional update result.
     * @return true when the timeline update completed without an execution error.
     */
    bool slayer3d_game_data_update_timeline(slayer3d_game_data_runtime *runtime,
                                            slayer3d_game_data_timeline_state *state, float dt,
                                            slayer3d_game_data_timeline_update_result *out_result);

    /**
     * @brief Advance data-authored runtime animations.
     *
     * Animations are started by generic actions such as `ui.animate` and
     * `property.animate`. This function advances active tweens, applies eased
     * values to UI runtime state or actor properties, emits completion signals
     * for one-shot animations, and clears scene-scoped animation state when the
     * active scene changes.
     *
     * @param runtime Runtime created by slayer3d_game_data_load_file().
     * @param dt Delta time in seconds.
     * @return true when active animations were advanced successfully.
     */
    bool slayer3d_game_data_update_animations(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Resolve UI text content from data-authored bindings.
     *
     * Literal `text` entries are copied directly. Entries with `bindings`
     * resolve engine metrics, brush diagnostics, scene state, and actor
     * properties, then format them using the descriptor's `format` string.
     */
    bool slayer3d_game_data_format_ui_text(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_ui_text *text,
                                           const slayer3d_game_data_ui_metrics *metrics, char *buffer,
                                           size_t buffer_size);

    /**
     * @brief Advance data-authored property animation components.
     *
     * Currently supports `property.decay` components, which move a numeric
     * actor property toward a target value at an authored rate. This is useful
     * for reusable presentation state such as flashes, glow weights, and other
     * transient values without per-game C code.
     */
    bool slayer3d_game_data_update_property_effects(slayer3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Return the dt currently being processed by slayer3d_game_data_update().
     *
     * Adapter callbacks can use this for per-frame controller behavior. Outside
     * an update call, this returns the most recent non-negative update dt.
     */
    float slayer3d_game_data_delta_time(const slayer3d_game_data_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_H */

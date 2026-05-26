/**
 * @file game_data_world_runtime.h
 * @brief Runtime world queries for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_WORLD_RUNTIME_H
#define SLAYER3D_GAME_DATA_WORLD_RUNTIME_H

#include <stdbool.h>

#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_world.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/render_context.h"

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

    /** @brief Active-scene world-space variant of slayer3d_game_data_slide_brush_world(). */
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

    /** @brief Return whether an authored sector navigation path exists between two positions. */
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

#ifdef __cplusplus
}
#endif

#endif

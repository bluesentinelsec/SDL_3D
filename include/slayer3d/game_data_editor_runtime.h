/**
 * @file game_data_editor_runtime.h
 * @brief Public editor runtime APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_EDITOR_RUNTIME_H
#define SLAYER3D_GAME_DATA_EDITOR_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game_data_brush.h"
#include "slayer3d/game_data_editor.h"
#include "slayer3d/game_data_world_model.h"
#include "slayer3d/properties.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

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
     * increments its editor revision. Source-backed worlds validate and commit
     * a canonical source box first, then rebuild runtime brushes from that
     * source. Structural source boxes may touch or overlap; overlap is reported
     * as source-model diagnostics and resolved by the compiler path instead of
     * blocking authoring. Runtime-only worlds receive stable editor metadata on
     * the brush and generated faces. @p out_brush_name receives the final
     * runtime brush name when non-NULL.
     */
    bool slayer3d_game_data_create_box_brush(slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_create_box_brush_desc *desc, char *out_brush_name,
                                             size_t out_brush_name_size, char *error_buffer, int error_buffer_size);

    /** @brief Runtime-authored actor placement marker for editor workflows. */
    typedef struct slayer3d_game_data_editor_actor
    {
        /** @brief Stable actor placement name. Pointer is runtime-owned. */
        const char *name;
        /** @brief Scene where this actor placement is valid, or NULL for scene-agnostic actors. */
        const char *scene;
        /** @brief Human-facing label for the actor palette/inspector. */
        const char *display_name;
        /** @brief Actor archetype id to instantiate when exported/test-run. */
        const char *archetype;
        /** @brief Editor mesh primitive name, such as capsule, box, or rectangle. */
        const char *mesh;
        /** @brief Optional render model asset id for finished models. */
        const char *model;
        /** @brief Palette category/group, such as Player, Opponents, or Sensors. */
        const char *group;
        /** @brief Actor world position in meters. */
        slayer3d_vec3 position;
        /** @brief Euler rotation in radians. */
        slayer3d_vec3 rotation;
        /** @brief Per-axis actor scale. */
        slayer3d_vec3 scale;
        /** @brief Editor display color, including alpha/transparency. */
        slayer3d_color color;
        /** @brief Source prefab id when this actor was placed from a prefab. */
        const char *prefab;
        /** @brief True while this actor receives shared updates from its source prefab. */
        bool prefab_linked;
        /** @brief Property keys that this actor overrides from its prefab. Pointer is runtime-owned. */
        const slayer3d_properties *prefab_overrides;
        /** @brief Arbitrary designer-authored properties. Pointer is runtime-owned. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_editor_actor;

    /** @brief Editor save state for actor placement markers. */
    typedef struct slayer3d_game_data_editor_actor_state
    {
        /** @brief True when runtime mutations have not been marked saved. */
        bool dirty;
        /** @brief Monotonic runtime mutation revision. */
        Uint64 revision;
        /** @brief Number of editor actors currently loaded in the runtime. */
        int count;
    } slayer3d_game_data_editor_actor_state;

    /** @brief Descriptor for creating or updating one editor actor placement. */
    typedef struct slayer3d_game_data_place_editor_actor_desc
    {
        /** @brief Actor placement name. Optional when @p name_prefix is provided. */
        const char *name;
        /** @brief Prefix used to generate a unique placement name when @p name is omitted. */
        const char *name_prefix;
        /** @brief Optional scene reference. Defaults to the active scene when omitted. */
        const char *scene;
        /** @brief Human-facing label for the actor palette/inspector. */
        const char *display_name;
        /** @brief Actor archetype id to instantiate when exported/test-run. */
        const char *archetype;
        /** @brief Editor mesh primitive name, such as capsule, box, or rectangle. */
        const char *mesh;
        /** @brief Optional render model asset id for finished models. */
        const char *model;
        /** @brief Palette category/group. */
        const char *group;
        /** @brief Actor position. Used only when @p has_position is true. */
        slayer3d_vec3 position;
        /** @brief Whether @p position is explicit. Defaults to selection point or placement preview. */
        bool has_position;
        /** @brief Actor Euler rotation in radians. */
        slayer3d_vec3 rotation;
        /** @brief Whether @p rotation is explicit. */
        bool has_rotation;
        /** @brief Actor scale. */
        slayer3d_vec3 scale;
        /** @brief Whether @p scale is explicit. */
        bool has_scale;
        /** @brief Editor display color, including alpha/transparency. */
        slayer3d_color color;
        /** @brief Whether @p color is explicit. */
        bool has_color;
        /** @brief Optional source prefab id for linked instances. */
        const char *prefab;
        /** @brief Whether this actor should receive shared updates from its source prefab. */
        bool prefab_linked;
        /** @brief Property keys explicitly overridden by this instance. */
        const slayer3d_properties *prefab_overrides;
        /** @brief Optional arbitrary designer-authored properties to copy into the actor. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_place_editor_actor_desc;

    /**
     * @brief Query one editor actor placement by name.
     *
     * Returned pointers are runtime-owned and remain valid until editor actors
     * are mutated or the runtime is destroyed.
     */
    bool slayer3d_game_data_get_editor_actor(const slayer3d_game_data_runtime *runtime, const char *name,
                                             slayer3d_game_data_editor_actor *out_actor);

    /** @brief Query editor save state for runtime actor placements. */
    bool slayer3d_game_data_get_editor_actor_state(const slayer3d_game_data_runtime *runtime,
                                                   slayer3d_game_data_editor_actor_state *out_state);

    /** @brief Create or update one runtime editor actor placement. */
    bool slayer3d_game_data_place_editor_actor(slayer3d_game_data_runtime *runtime,
                                               const slayer3d_game_data_place_editor_actor_desc *desc, char *out_name,
                                               size_t out_name_size, char *error_buffer, int error_buffer_size);

    /** @brief First-class prefab definition for reusable editor placements. */
    typedef struct slayer3d_game_data_editor_prefab
    {
        /** @brief Stable prefab id. Pointer is runtime-owned. */
        const char *id;
        /** @brief Human-facing browser label. Pointer is runtime-owned. */
        const char *label;
        /** @brief Browser category/group. Pointer is runtime-owned. */
        const char *category;
        /** @brief Prefab kind, such as actor, brush, or mixed. Pointer is runtime-owned. */
        const char *kind;
        /** @brief Actor archetype default for actor prefabs. */
        const char *archetype;
        /** @brief Editor mesh primitive default for actor prefabs. */
        const char *mesh;
        /** @brief Optional model asset id default for actor prefabs. */
        const char *model;
        /** @brief Actor group default for actor prefabs. */
        const char *group;
        /** @brief Default placement position offset. */
        slayer3d_vec3 position;
        /** @brief Default placement rotation. */
        slayer3d_vec3 rotation;
        /** @brief Default placement scale. */
        slayer3d_vec3 scale;
        /** @brief Default editor display color. */
        slayer3d_color color;
        /** @brief Shared prefab properties copied to linked instances. Pointer is runtime-owned. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_editor_prefab;

    /** @brief Editor save state for prefab definitions. */
    typedef struct slayer3d_game_data_editor_prefab_state
    {
        /** @brief True when runtime mutations have not been marked saved. */
        bool dirty;
        /** @brief Monotonic runtime mutation revision. */
        Uint64 revision;
        /** @brief Number of prefab definitions currently loaded in the runtime. */
        int count;
    } slayer3d_game_data_editor_prefab_state;

    /** @brief Descriptor for creating or updating one editor prefab definition. */
    typedef struct slayer3d_game_data_place_editor_prefab_desc
    {
        /** @brief Stable prefab id. Required. */
        const char *id;
        /** @brief Human-facing browser label. */
        const char *label;
        /** @brief Browser category/group. */
        const char *category;
        /** @brief Prefab kind, defaults to actor. */
        const char *kind;
        /** @brief Actor archetype default for actor prefabs. */
        const char *archetype;
        /** @brief Editor mesh primitive default for actor prefabs. */
        const char *mesh;
        /** @brief Optional model asset id default for actor prefabs. */
        const char *model;
        /** @brief Actor group default for actor prefabs. */
        const char *group;
        /** @brief Default placement position offset. */
        slayer3d_vec3 position;
        /** @brief Whether @p position is explicit. */
        bool has_position;
        /** @brief Default placement rotation. */
        slayer3d_vec3 rotation;
        /** @brief Whether @p rotation is explicit. */
        bool has_rotation;
        /** @brief Default placement scale. */
        slayer3d_vec3 scale;
        /** @brief Whether @p scale is explicit. */
        bool has_scale;
        /** @brief Default editor display color. */
        slayer3d_color color;
        /** @brief Whether @p color is explicit. */
        bool has_color;
        /** @brief Shared prefab properties copied to linked instances. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_place_editor_prefab_desc;

    /** @brief Descriptor for placing a linked prefab instance. */
    typedef struct slayer3d_game_data_instantiate_editor_prefab_desc
    {
        /** @brief Source prefab id. Required. */
        const char *prefab;
        /** @brief Optional actor instance name. */
        const char *name;
        /** @brief Prefix used to generate an instance name when @p name is omitted. */
        const char *name_prefix;
        /** @brief Optional scene reference. Defaults to the active scene. */
        const char *scene;
        /** @brief Optional placement position override. */
        slayer3d_vec3 position;
        /** @brief Whether @p position is explicit. */
        bool has_position;
        /** @brief Optional placement rotation override. */
        slayer3d_vec3 rotation;
        /** @brief Whether @p rotation is explicit. */
        bool has_rotation;
        /** @brief Optional placement scale override. */
        slayer3d_vec3 scale;
        /** @brief Whether @p scale is explicit. */
        bool has_scale;
        /** @brief Optional property values to apply as instance overrides. */
        const slayer3d_properties *properties;
        /** @brief Property keys explicitly overridden by this instance. */
        const slayer3d_properties *prefab_overrides;
    } slayer3d_game_data_instantiate_editor_prefab_desc;

    /** @brief Query one prefab definition by id. */
    bool slayer3d_game_data_get_editor_prefab(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_game_data_editor_prefab *out_prefab);

    /** @brief Query editor save state for runtime prefab definitions. */
    bool slayer3d_game_data_get_editor_prefab_state(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_prefab_state *out_state);

    /** @brief Create or update one runtime prefab definition and propagate shared updates to linked instances. */
    bool slayer3d_game_data_place_editor_prefab(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_place_editor_prefab_desc *desc,
                                                char *error_buffer, int error_buffer_size);

    /** @brief Instantiate one actor prefab as a linked editor actor. */
    bool slayer3d_game_data_instantiate_editor_prefab(slayer3d_game_data_runtime *runtime,
                                                      const slayer3d_game_data_instantiate_editor_prefab_desc *desc,
                                                      char *out_name, size_t out_name_size, char *error_buffer,
                                                      int error_buffer_size);

    /** @brief Detach an actor instance from future shared prefab updates. */
    bool slayer3d_game_data_unlink_editor_actor_prefab(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                                       char *error_buffer, int error_buffer_size);

    /** @brief One endpoint in an editor-authored generic connection. */
    typedef struct slayer3d_game_data_editor_connection_endpoint
    {
        /** @brief Referenced object/entity id, or external id when @p external is true. */
        const char *entity;
        /** @brief Source-side event name, used by connection sources. */
        const char *event;
        /** @brief Target-side action name, used by connection targets. */
        const char *action;
        /** @brief True when the endpoint is intentionally outside this map. */
        bool external;
    } slayer3d_game_data_editor_connection_endpoint;

    /** @brief Runtime-authored generic connection between editor objects. */
    typedef struct slayer3d_game_data_editor_connection
    {
        /** @brief Stable connection id. Pointer is runtime-owned. */
        const char *id;
        /** @brief Source endpoint. */
        slayer3d_game_data_editor_connection_endpoint from;
        /** @brief Target endpoint. */
        slayer3d_game_data_editor_connection_endpoint to;
        /** @brief Arbitrary designer-authored connection properties. Pointer is runtime-owned. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_editor_connection;

    /** @brief Editor save state for generic connections. */
    typedef struct slayer3d_game_data_editor_connection_state
    {
        /** @brief True when runtime mutations have not been marked saved. */
        bool dirty;
        /** @brief Monotonic runtime mutation revision. */
        Uint64 revision;
        /** @brief Number of editor connections currently loaded in the runtime. */
        int count;
    } slayer3d_game_data_editor_connection_state;

    /** @brief Descriptor for creating or updating one generic editor connection. */
    typedef struct slayer3d_game_data_place_editor_connection_desc
    {
        /** @brief Connection id. Optional when @p id_prefix is provided. */
        const char *id;
        /** @brief Prefix used to generate a unique connection id when @p id is omitted. */
        const char *id_prefix;
        /** @brief Source endpoint. */
        slayer3d_game_data_editor_connection_endpoint from;
        /** @brief Target endpoint. */
        slayer3d_game_data_editor_connection_endpoint to;
        /** @brief Optional arbitrary designer-authored connection properties to copy. */
        const slayer3d_properties *properties;
    } slayer3d_game_data_place_editor_connection_desc;

    /**
     * @brief Query one editor connection by id.
     *
     * Returned pointers are runtime-owned and remain valid until editor
     * connections are mutated or the runtime is destroyed.
     */
    bool slayer3d_game_data_get_editor_connection(const slayer3d_game_data_runtime *runtime, const char *id,
                                                  slayer3d_game_data_editor_connection *out_connection);

    /** @brief Query editor save state for runtime generic connections. */
    bool slayer3d_game_data_get_editor_connection_state(const slayer3d_game_data_runtime *runtime,
                                                        slayer3d_game_data_editor_connection_state *out_state);

    /** @brief Create or update one runtime generic editor connection. */
    bool slayer3d_game_data_place_editor_connection(slayer3d_game_data_runtime *runtime,
                                                    const slayer3d_game_data_place_editor_connection_desc *desc,
                                                    char *out_id, size_t out_id_size, char *error_buffer,
                                                    int error_buffer_size);

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
     * @brief Export one editable level as a standalone Slayer3D map JSON document.
     *
     * The exported document uses the public `slayer3d.map` format and embeds
     * the lossless editable level fragment in `editor.editable_level_fragment`.
     * Top-level materials, brushes, and player-start actors are also emitted so
     * generic tools can inspect the authored level without understanding the
     * editor-only fragment contract. The returned string is allocated with
     * SDL_malloc and must be released with SDL_free().
     */
    bool slayer3d_game_data_export_editable_level_map_json(const slayer3d_game_data_runtime *runtime,
                                                           const char *world_name, char **out_json, size_t *out_size,
                                                           char *error_buffer, int error_buffer_size);

    /**
     * @brief Atomically save one editable level as a standalone Slayer3D map file.
     *
     * This saves the same JSON produced by
     * @ref slayer3d_game_data_export_editable_level_map_json. On success, both
     * the selected brush world and the player-start collection are marked saved
     * at their current revisions and @p path becomes their editor source path.
     */
    bool slayer3d_game_data_save_editable_level_map_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                         const char *path, size_t *out_size, char *error_buffer,
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

    /**
     * @brief Load a Slayer3D map JSON buffer into an existing editor runtime.
     *
     * The map is validated through the public map APIs, then
     * `editor.editable_level_fragment` is imported into @p world_name. This
     * preserves the current editor source model while the standalone map format
     * evolves toward direct runtime construction.
     */
    bool slayer3d_game_data_load_editable_level_map_json(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                         const void *json, size_t json_size, const char *source_path,
                                                         char *error_buffer, int error_buffer_size);

    /**
     * @brief Load a Slayer3D map file into an existing editor runtime.
     *
     * The file must be a valid `slayer3d.map` JSON document containing
     * `editor.editable_level_fragment`. On success both runtime collections are
     * marked clean and @p path becomes their editor source path.
     */
    bool slayer3d_game_data_load_editable_level_map_file(slayer3d_game_data_runtime *runtime, const char *world_name,
                                                         const char *path, char *error_buffer, int error_buffer_size);

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

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_EDITOR_RUNTIME_H */

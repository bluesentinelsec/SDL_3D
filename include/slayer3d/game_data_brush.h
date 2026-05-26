/**
 * @file game_data_brush.h
 * @brief Public brush-world data descriptors for JSON-authored games.
 */

#ifndef SLAYER3D_GAME_DATA_BRUSH_H
#define SLAYER3D_GAME_DATA_BRUSH_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game_data_editor_metadata.h"
#include "slayer3d/model.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        /** @brief Brush blocks actor and projectile movement unless masked out by caller. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID = 1u << 0,
        /** @brief Brush blocks player movement but may be ignored by other traces. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP = 1u << 1,
        /** @brief Brush blocks projectile traces but may be ignored by actors. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP = 1u << 2,
        /** @brief Brush is a gameplay trigger volume rather than blocking geometry. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_TRIGGER = 1u << 3,
        /** @brief Brush represents water contents. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_WATER = 1u << 4,
        /** @brief Brush represents lava or damaging liquid contents. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_LAVA = 1u << 5,
        /** @brief Brush surface should be treated as sky by renderers. */
        SLAYER3D_GAME_DATA_BRUSH_CONTENT_SKY = 1u << 6,
    };

    enum
    {
        /** @brief Face does not emit collision geometry. */
        SLAYER3D_GAME_DATA_BRUSH_SURFACE_NO_COLLIDE = 1u << 0,
        /** @brief Face has low-friction movement semantics. */
        SLAYER3D_GAME_DATA_BRUSH_SURFACE_SLICK = 1u << 1,
        /** @brief Face can be climbed by compatible controllers. */
        SLAYER3D_GAME_DATA_BRUSH_SURFACE_LADDER = 1u << 2,
        /** @brief Face should contribute emissive material response. */
        SLAYER3D_GAME_DATA_BRUSH_SURFACE_EMISSIVE = 1u << 3,
        /** @brief Face is a candidate for future portal/visibility tooling. */
        SLAYER3D_GAME_DATA_BRUSH_SURFACE_PORTAL_CANDIDATE = 1u << 4,
    };

    /** @brief Runtime-owned material referenced by authored brush faces. */
    typedef struct slayer3d_game_data_brush_material
    {
        /** @brief Stable authored material name. */
        const char *name;
        /** @brief Optional asset:// texture image path. */
        const char *texture;
        /** @brief Linear RGBA material factor, default white. */
        slayer3d_vec4 albedo;
        /** @brief Authored metallic factor for future material systems. */
        float metallic;
        /** @brief Authored roughness factor for future material systems. */
        float roughness;
        /** @brief Additive emissive RGB material factor. */
        slayer3d_vec3 emissive;
        /** @brief Positive texture scale hint. */
        float tex_scale;
        /** @brief Optional editor/tooling metadata. */
        slayer3d_game_data_editor_metadata editor;
    } slayer3d_game_data_brush_material;

    /** @brief One plane-bounded face on an authored convex brush. */
    typedef struct slayer3d_game_data_brush_face
    {
        /** @brief Plane normal. Validation requires a non-zero vector. */
        slayer3d_vec3 normal;
        /** @brief Plane distance in normal-dot-position form. */
        float distance;
        /** @brief Index into the brush world's material array. */
        int material_index;
        /** @brief Resolved material name, or NULL when no material was authored. */
        const char *material_name;
        /** @brief Per-face UV scale multiplier, default {1, 1}. */
        float uv_scale[2];
        /** @brief Per-face UV offset, applied after scale/rotation. */
        float uv_offset[2];
        /** @brief Per-face UV rotation in degrees. */
        float uv_rotation_degrees;
        /** @brief Bitmask of SLAYER3D_GAME_DATA_BRUSH_SURFACE_* flags. */
        unsigned int surface_flags;
        /** @brief Optional editor/tooling metadata. */
        slayer3d_game_data_editor_metadata editor;
    } slayer3d_game_data_brush_face;

    /** @brief Authored per-brush visibility override for automatic occlusion. */
    typedef enum slayer3d_game_data_brush_visibility
    {
        /** @brief Let the renderer decide visibility from the brush world visibility grid. */
        SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_AUTO = 0,
        /** @brief Never hide this brush through visibility occlusion. */
        SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_ALWAYS = 1,
        /** @brief Use explicit camera-to-brush trace fallback if the automatic grid is unavailable. */
        SLAYER3D_GAME_DATA_BRUSH_VISIBILITY_TRACE = 2,
    } slayer3d_game_data_brush_visibility;

    /** @brief Authored convex brush loaded into runtime-owned data. */
    typedef struct slayer3d_game_data_brush
    {
        /** @brief Stable authored brush name. */
        const char *name;
        /** @brief Bitmask of SLAYER3D_GAME_DATA_BRUSH_CONTENT_* flags. */
        unsigned int contents;
        /** @brief True when runtime visibility occlusion may cull this brush. */
        bool visibility_cullable;
        /** @brief Authored visibility override, normally automatic. */
        slayer3d_game_data_brush_visibility visibility;
        /** @brief Optional authored tags for editor/runtime queries. */
        const char *const *tags;
        /** @brief Number of entries in @p tags. */
        int tag_count;
        /** @brief Convex brush faces. */
        const slayer3d_game_data_brush_face *faces;
        /** @brief Number of entries in @p faces. */
        int face_count;
        /** @brief Precomputed local-space bounds for broad-phase trace rejection. */
        slayer3d_bounding_box bounds;
        /** @brief True when @p bounds is valid. */
        bool has_bounds;
        /** @brief Optional editor/tooling metadata. */
        slayer3d_game_data_editor_metadata editor;
    } slayer3d_game_data_brush;

    /** @brief Runtime-compiled brush-world chunk for broad-phase collision/render tooling. */
    typedef struct slayer3d_game_data_brush_compile_chunk
    {
        /** @brief Local-space bounds spanning all brushes assigned to this chunk. */
        slayer3d_bounding_box bounds;
        /** @brief True when @p bounds is valid. */
        bool has_bounds;
        /** @brief Union of contents bits from brushes assigned to this chunk. */
        unsigned int contents_mask;
        /** @brief Indices into the owning brush world's authored brush array. */
        const int *brush_indices;
        /** @brief Number of entries in @p brush_indices. */
        int brush_count;
    } slayer3d_game_data_brush_compile_chunk;

    /** @brief One visible source brush face emitted into a compiled brush-world render mesh. */
    typedef struct slayer3d_game_data_brush_compiled_face
    {
        /** @brief Brush index in the owning brush world. */
        int brush_index;
        /** @brief Face index in the owning brush. */
        int face_index;
        /** @brief Material index used by this compiled face. */
        int material_index;
        /** @brief Render-model mesh index that contains this face's triangles. */
        int mesh_index;
        /** @brief First vertex for this face inside @p mesh_index. */
        int first_vertex;
        /** @brief Number of vertices emitted for this face. */
        int vertex_count;
        /** @brief Number of triangles emitted for this face. */
        int triangle_count;
        /** @brief Authored brush name, or NULL. */
        const char *brush_name;
        /** @brief Authored material name, or NULL. */
        const char *material_name;
        /** @brief Stable source brush id from editor metadata, or NULL. */
        const char *source_brush_stable_id;
        /** @brief Stable source face id from editor metadata, or NULL. */
        const char *source_face_stable_id;
    } slayer3d_game_data_brush_compiled_face;

    /** @brief Runtime-owned native brush world. */
    typedef struct slayer3d_game_data_brush_world
    {
        /** @brief Stable authored brush world name. */
        const char *name;
        /** @brief Authored unit label, normally `meters`. */
        const char *units;
        /** @brief Conversion factor from authored units to meters. */
        float meters_per_unit;
        /** @brief Positive cell size for automatic visibility culling, in local world units. */
        float visibility_cell_size;
        /** @brief True when fully hidden adjacent solid faces are removed from compiled render meshes. */
        bool compile_hidden_face_culling;
        /** @brief Optional authored spatial compile chunk cell size, or <= 0 for automatic sizing. */
        float compile_chunk_cell_size_hint;
        /** @brief Runtime material palette for brush faces. */
        const slayer3d_game_data_brush_material *materials;
        /** @brief Number of entries in @p materials. */
        int material_count;
        /** @brief Authored convex brushes. */
        const slayer3d_game_data_brush *brushes;
        /** @brief Number of entries in @p brushes. */
        int brush_count;
        /** @brief Runtime-compiled static render mesh for visible brush faces, or NULL when empty. */
        const slayer3d_model *render_model;
        /** @brief Renderable brush faces considered by the compile step. */
        int compile_face_count;
        /** @brief Renderable brush faces emitted to the compiled render model. */
        int compile_rendered_face_count;
        /** @brief Renderable brush faces hidden by adjacent solid brushes during compile. */
        int compile_culled_face_count;
        /** @brief Triangles emitted to the compiled render model after compile-time face culling. */
        int compile_triangle_count;
        /** @brief Source brush/face metadata for each visible face emitted to the compiled render model. */
        const slayer3d_game_data_brush_compiled_face *compile_rendered_faces;
        /** @brief Number of entries in @p compile_rendered_faces. */
        int compile_rendered_face_metadata_count;
        /** @brief Authored brushes that failed to produce bounded geometry during compile. */
        int compile_invalid_brush_count;
        /** @brief Authored brush faces that produced fewer than three clipped vertices during compile. */
        int compile_degenerate_face_count;
        /** @brief Spatial compile chunks used by broad-phase collision/tooling queries. */
        const slayer3d_game_data_brush_compile_chunk *compile_chunks;
        /** @brief Number of entries in @p compile_chunks. */
        int compile_chunk_count;
        /** @brief Contiguous storage backing each chunk's brush_indices pointer. */
        const int *compile_chunk_brush_indices;
        /** @brief Number of entries in @p compile_chunk_brush_indices. */
        int compile_chunk_brush_index_count;
        /** @brief Local-space cell size used to build the compile chunks. */
        float compile_chunk_cell_size;
        /** @brief Deterministic hash of the compiled render/chunk artifact metadata. */
        Uint64 compile_artifact_hash;
        /** @brief Precomputed local-space bounds spanning all bounded brushes. */
        slayer3d_bounding_box bounds;
        /** @brief True when @p bounds is valid. */
        bool has_bounds;
        /** @brief Optional editor/tooling metadata. */
        slayer3d_game_data_editor_metadata editor;
    } slayer3d_game_data_brush_world;

    /** @brief Result of comparing a brush compile artifact manifest to a runtime brush world. */
    typedef struct slayer3d_game_data_brush_compile_artifact_status
    {
        /** @brief True when the JSON document has the expected artifact schema. */
        bool schema_matches;
        /** @brief True when the manifest names the requested brush world. */
        bool world_matches;
        /** @brief True when the manifest source hash matches the current authored brush source. */
        bool source_hash_matches;
        /** @brief True when the manifest compile policy matches the current authored compile policy. */
        bool policy_matches;
        /** @brief True when the manifest editor source-model metadata matches the current runtime world. */
        bool source_model_matches;
        /** @brief True when the manifest compiled artifact hash matches the current runtime artifact. */
        bool compile_artifact_hash_matches;
        /** @brief True when all fields required for artifact reuse match. */
        bool fresh;
        /** @brief Current runtime source hash for authored brush inputs. */
        Uint64 expected_source_hash;
        /** @brief Source hash stored in the manifest. */
        Uint64 artifact_source_hash;
        /** @brief Current runtime compiled artifact hash. */
        Uint64 expected_compile_artifact_hash;
        /** @brief Compiled artifact hash stored in the manifest. */
        Uint64 artifact_compile_artifact_hash;
        /** @brief True when the current runtime world was compiled from editor source boxes. */
        bool expected_source_model_present;
        /** @brief True when the manifest reports editor source boxes. */
        bool artifact_source_model_present;
        /** @brief Current runtime editor source box count, or 0 for runtime-only worlds. */
        int expected_source_box_count;
        /** @brief Editor source box count stored in the manifest, or 0 when absent. */
        int artifact_source_box_count;
    } slayer3d_game_data_brush_compile_artifact_status;

    /** @brief Maximum bytes, including the NUL terminator, for brush compile artifact layout paths. */
#define SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX 1024

    /** @brief Canonical filesystem layout for one offline brush compile artifact. */
    typedef struct slayer3d_game_data_brush_compile_artifact_layout
    {
        /** @brief Stable filesystem-safe key derived from the brush world name. */
        char world_key[128];
        /** @brief Directory containing this exact source/policy artifact. */
        char directory[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
        /** @brief JSON descriptor path produced by the current manifest exporter. */
        char manifest_path[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
        /** @brief Reserved path for future compiled render-mesh payload data. */
        char render_payload_path[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
        /** @brief Reserved path for future compiled collision payload data. */
        char collision_payload_path[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
        /** @brief Reserved path for future compiled visibility-grid payload data. */
        char visibility_payload_path[SLAYER3D_GAME_DATA_BRUSH_COMPILE_ARTIFACT_LAYOUT_PATH_MAX];
        /** @brief Current runtime source hash for authored brush inputs. */
        Uint64 source_hash;
        /** @brief Current runtime compiled artifact hash for source plus compile policy. */
        Uint64 compile_artifact_hash;
    } slayer3d_game_data_brush_compile_artifact_layout;

    /** @brief Active-scene instance of an authored brush world. */
    typedef struct slayer3d_game_data_brush_world_instance
    {
        /** @brief Authored brush world name. */
        const char *world_name;
        /** @brief Runtime-owned brush world descriptor. */
        const slayer3d_game_data_brush_world *world;
        /** @brief World-space translation applied before rendering/collision queries. */
        slayer3d_vec3 position;
        /** @brief Whether renderers/collision systems should use future acceleration data. */
        bool acceleration_enabled;
        /** @brief Whether brush surfaces participate in dynamic lighting for this instance. */
        bool lighting_enabled;
        /** @brief Whether debug wireframe should be requested for this instance. */
        bool debug_wireframe;
        /** @brief Whether camera-based brush visibility/occlusion should be evaluated before drawing. */
        bool visibility_occlusion_enabled;
    } slayer3d_game_data_brush_world_instance;

    /** @brief Collision shape used by a brush-world trace. */
    typedef enum slayer3d_game_data_brush_trace_shape
    {
        /** @brief Infinitesimal point/ray trace. */
        SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT = 0,
        /** @brief Swept sphere trace with radius from `extents.x`. */
        SLAYER3D_GAME_DATA_BRUSH_TRACE_SPHERE = 1,
        /** @brief Swept axis-aligned box trace using xyz half-extents. */
        SLAYER3D_GAME_DATA_BRUSH_TRACE_AABB = 2,
    } slayer3d_game_data_brush_trace_shape;

    /** @brief Input descriptor for tracing through authored brush worlds. */
    typedef struct slayer3d_game_data_brush_trace_desc
    {
        /** @brief Trace start point in world coordinates. */
        slayer3d_vec3 start;
        /** @brief Trace end point in world coordinates. */
        slayer3d_vec3 end;
        /** @brief Shape to sweep along start-to-end. */
        slayer3d_game_data_brush_trace_shape shape;
        /** @brief Sphere radius in x, or AABB half-extents for xyz. */
        slayer3d_vec3 extents;
        /** @brief Bitmask of SLAYER3D_GAME_DATA_BRUSH_CONTENT_* flags to collide with. */
        unsigned int contents_mask;
    } slayer3d_game_data_brush_trace_desc;

    /** @brief Deterministic result from an authored brush-world trace. */
    typedef struct slayer3d_game_data_brush_trace_result
    {
        /** @brief True when the trace touches a brush matching the contents mask. */
        bool hit;
        /** @brief True when the trace starts inside a matching brush. */
        bool start_solid;
        /** @brief True when the trace remains inside a matching brush. */
        bool all_solid;
        /** @brief First hit fraction in [0, 1] along start-to-end. */
        float fraction;
        /** @brief End position at @p fraction. */
        slayer3d_vec3 end_position;
        /** @brief Hit point on the swept shape origin path. */
        slayer3d_vec3 point;
        /** @brief Outward collision plane normal, or zero when starting solid. */
        slayer3d_vec3 normal;
        /** @brief Authored brush world name. */
        const char *world_name;
        /** @brief World-space translation of the hit brush-world instance. */
        slayer3d_vec3 world_position;
        /** @brief Authored brush name. */
        const char *brush_name;
        /** @brief Authored face material name for plane hits, or NULL. */
        const char *material_name;
        /** @brief Brush index in the authored world, or -1. */
        int brush_index;
        /** @brief Face index that produced the collision normal, or -1. */
        int face_index;
        /** @brief Contents bitmask on the hit brush. */
        unsigned int contents;
        /** @brief Surface flags on the hit face. */
        unsigned int surface_flags;
    } slayer3d_game_data_brush_trace_result;

    /** @brief Runtime counters for brush-world trace broad-phase and narrow-phase work. */
    typedef struct slayer3d_game_data_brush_diagnostics
    {
        /** @brief Number of valid local brush-world trace evaluations. */
        Uint64 trace_count;
        /** @brief Active-scene brush world instances visited by trace requests. */
        Uint64 world_instance_count;
        /** @brief Active-scene brush world instances rejected by world bounds. */
        Uint64 world_bounds_reject_count;
        /** @brief Authored brushes considered by local trace loops. */
        Uint64 brush_count;
        /** @brief Visited brushes rejected because contents did not overlap the trace mask. */
        Uint64 contents_reject_count;
        /** @brief Brushes rejected by precomputed brush bounds. */
        Uint64 bounds_reject_count;
        /** @brief Brushes that reached exact plane-based trace tests. */
        Uint64 collision_candidate_count;
        /** @brief Local brush-world trace evaluations that produced a hit. */
        Uint64 hit_count;
        /** @brief Brush-world model meshes submitted to the renderer. */
        Uint64 render_mesh_submissions;
        /** @brief Brush-world model meshes rejected by renderer frustum culling. */
        Uint64 render_mesh_culled;
        /** @brief Brush-world model meshes accepted by the renderer. */
        Uint64 render_mesh_draws;
        /** @brief Approximate brush-world triangles submitted after renderer culling. */
        Uint64 render_triangles_submitted;
        /** @brief Renderable brush faces considered by brush-world compile steps. */
        Uint64 compile_face_count;
        /** @brief Renderable brush faces emitted by brush-world compile steps. */
        Uint64 compile_rendered_face_count;
        /** @brief Renderable brush faces hidden by adjacent solid brushes during compile. */
        Uint64 compile_culled_face_count;
        /** @brief Triangles emitted by brush-world compile steps after compile-time face culling. */
        Uint64 compile_triangle_count;
        /** @brief Authored brushes that failed to produce bounded geometry during compile. */
        Uint64 compile_invalid_brush_count;
        /** @brief Authored brush faces that produced fewer than three clipped vertices during compile. */
        Uint64 compile_degenerate_face_count;
        /** @brief Spatial chunks emitted by brush-world compile steps. */
        Uint64 compile_chunk_count;
        /** @brief Collision chunks tested by brush-world trace broad-phase. */
        Uint64 collision_chunk_count;
        /** @brief Collision chunks rejected by bounds or contents masks. */
        Uint64 collision_chunk_reject_count;
        /** @brief Brush-world compile chunk models drawn instead of per-brush visibility models. */
        Uint64 render_chunk_draws;
        /** @brief Visible brushes covered by chunk-level draw submissions. */
        Uint64 render_chunk_brushes_drawn;
        /** @brief Renderable brushes tested by brush-level frustum culling before draw submission. */
        Uint64 frustum_brush_candidates;
        /** @brief Renderable brushes rejected by brush-level frustum culling before draw submission. */
        Uint64 frustum_brush_culled;
        /** @brief Approximate triangles skipped by brush-level frustum culling before draw submission. */
        Uint64 frustum_triangles_culled;
        /** @brief Renderable brushes tested by brush-world visibility culling. */
        Uint64 visibility_brush_candidates;
        /** @brief Renderable brushes accepted by brush-world visibility culling. */
        Uint64 visibility_brush_visible;
        /** @brief Renderable brushes rejected by brush-world visibility culling. */
        Uint64 visibility_brush_occluded;
        /** @brief Approximate triangles skipped by brush-world visibility culling. */
        Uint64 visibility_triangles_culled;
        /** @brief Brush visibility-grid cells reused from a cached camera cell. */
        Uint64 visibility_grid_cache_hits;
        /** @brief Brush visibility-grid cells rebuilt for a new camera cell. */
        Uint64 visibility_grid_cache_misses;
    } slayer3d_game_data_brush_diagnostics;

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file game_data_render.h
 * @brief JSON-authored game data render descriptors and metrics.
 */

#ifndef SLAYER3D_GAME_DATA_RENDER_H
#define SLAYER3D_GAME_DATA_RENDER_H

#include <stdbool.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/lighting.h"
#include "slayer3d/math.h"
#include "slayer3d/render_context.h"
#include "slayer3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Runtime metrics used when evaluating data-authored UI bindings.
     *
     * Games provide this small host-state snapshot each frame. The game data
     * runtime combines it with actor properties and active camera state to
     * resolve UI visibility and text content without game-specific string maps.
     */
    typedef struct slayer3d_game_data_ui_metrics
    {
        /** @brief Whether the managed loop is currently paused. */
        bool paused;
        /** @brief Most recently sampled frames per second. */
        float fps;
        /** @brief Number of rendered frames. */
        Uint64 frame;
        /** @brief Sampled wall-clock frame time in milliseconds. */
        float frame_ms;
        /** @brief Sampled managed data-game update CPU time in milliseconds. */
        float update_cpu_ms;
        /** @brief Sampled managed data-game render CPU time in milliseconds, excluding GPU present. */
        float render_cpu_ms;
        /** @brief Sampled renderer model meshes submitted per frame. */
        float render_model_mesh_submissions_per_frame;
        /** @brief Sampled renderer model meshes accepted per frame. */
        float render_model_mesh_draws_per_frame;
        /** @brief Sampled renderer triangles submitted per frame. */
        float render_model_triangles_per_frame;
        /** @brief Sampled backend geometry draw calls per frame. */
        float render_geometry_draw_calls_per_frame;
        /** @brief Sampled backend instanced static mesh draw calls per frame. */
        float render_static_mesh_instanced_draw_calls_per_frame;
        /** @brief Sampled static mesh instances batched by the backend per frame. */
        float render_static_mesh_instances_batched_per_frame;
        /** @brief Sampled backend draw calls avoided by static mesh instancing per frame. */
        float render_static_mesh_draw_calls_saved_per_frame;
        /** @brief Sampled procedural LOD candidates per frame. */
        float render_procedural_lod_candidates_per_frame;
        /** @brief Sampled procedural primitives reduced by LOD per frame. */
        float render_procedural_lod_reduced_per_frame;
        /** @brief Sampled authored procedural triangle budget before LOD per frame. */
        float render_procedural_lod_authored_triangles_per_frame;
        /** @brief Sampled resolved procedural triangle budget after LOD per frame. */
        float render_procedural_lod_resolved_triangles_per_frame;
        /** @brief Sampled procedural triangles avoided by LOD per frame. */
        float render_procedural_lod_triangles_saved_per_frame;
        /** @brief Sampled model LOD candidates per frame. */
        float render_model_lod_candidates_per_frame;
        /** @brief Sampled model primitives culled by screen-space LOD per frame. */
        float render_model_lod_culled_per_frame;
        /** @brief Sampled model triangles avoided by screen-space LOD culling per frame. */
        float render_model_lod_triangles_saved_per_frame;
        /** @brief Sampled depth-prepass draw calls per frame. */
        float render_depth_prepass_draws_per_frame;
        /** @brief Sampled depth-prepass triangles per frame. */
        float render_depth_prepass_triangles_per_frame;
        /** @brief Sampled depth-prepass depth-passing samples per frame when performance queries are enabled. */
        float render_depth_prepass_samples_per_frame;
        /** @brief Sampled main-geometry depth-passing samples per frame when performance queries are enabled. */
        float render_geometry_samples_per_frame;
        /** @brief Sampled candidate lights considered by lit draws per frame. */
        float render_light_candidates_per_frame;
        /** @brief Sampled lights selected and uploaded by lit draws per frame. */
        float render_lights_selected_per_frame;
        /** @brief Sampled lit draws that performed per-object light selection per frame. */
        float render_light_selection_draws_per_frame;
        /** @brief Ratio of selected lights to candidate lights in the current metrics window. */
        float render_light_selection_ratio;
        /** @brief Current 3D/world render scale. */
        float render_world_scale;
        /** @brief Current 3D/world framebuffer width. */
        float render_world_width;
        /** @brief Current 3D/world framebuffer height. */
        float render_world_height;
        /** @brief Current desktop window backing pixel width. */
        float render_window_pixel_width;
        /** @brief Current desktop window backing pixel height. */
        float render_window_pixel_height;
        /** @brief Current SDL window pixel-density ratio. */
        float render_window_pixel_density;
    } slayer3d_game_data_ui_metrics;

    /** @brief Optional render evaluation inputs for dynamic visual effects. */
    typedef struct slayer3d_game_data_render_eval
    {
        /** @brief Elapsed presentation time in seconds, used by pulse effects. */
        float time;
    } slayer3d_game_data_render_eval;

    /** @brief Authored render primitive kind. */
    typedef enum slayer3d_game_data_render_primitive_type
    {
        /** @brief Axis-aligned cube/box primitive. */
        SLAYER3D_GAME_DATA_RENDER_CUBE = 1,
        /** @brief Sphere primitive. */
        SLAYER3D_GAME_DATA_RENDER_SPHERE = 2,
        /** @brief Batched sphere primitive instances sharing one shape/material. */
        SLAYER3D_GAME_DATA_RENDER_SPHERE_BATCH = 3,
        /** @brief Billboard sprite primitive backed by an authored sprite asset. */
        SLAYER3D_GAME_DATA_RENDER_SPRITE = 4,
        /** @brief 3D model primitive backed by an authored model asset. */
        SLAYER3D_GAME_DATA_RENDER_MODEL = 5,
        /** @brief Procedural mesh primitive selected by @ref slayer3d_game_data_mesh_primitive_kind. */
        SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE = 6,
    } slayer3d_game_data_render_primitive_type;

    /** @brief Procedural mesh shape selected by a `render.mesh_primitive` component. */
    typedef enum slayer3d_game_data_mesh_primitive_kind
    {
        /** @brief Invalid or unknown procedural mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_INVALID = 0,
        /** @brief Box/cube mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CUBE = 1,
        /** @brief Sphere mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_SPHERE = 2,
        /** @brief Capsule mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CAPSULE = 3,
        /** @brief Cylinder mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CYLINDER = 4,
        /** @brief Cone mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_CONE = 5,
        /** @brief Torus mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS = 6,
        /** @brief Square-pyramid mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PYRAMID = 7,
        /** @brief Wedge/ramp mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_WEDGE = 8,
        /** @brief Flat rectangular plane/quad mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_PLANE = 9,
        /** @brief Flat circular disc mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_DISC = 10,
        /** @brief Half-sphere dome mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_HEMISPHERE = 11,
        /** @brief Rounded box mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX = 12,
        /** @brief Curved pipe/tube segment mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT = 13,
        /** @brief Arrow marker mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ARROW = 14,
        /** @brief Flat vertical billboard-style plane mesh primitive. */
        SLAYER3D_GAME_DATA_MESH_PRIMITIVE_BILLBOARD_PLANE = 15,
    } slayer3d_game_data_mesh_primitive_kind;

    /** @brief Draw mode selected by authored procedural mesh primitives. */
    typedef enum slayer3d_game_data_render_draw_mode
    {
        /** @brief Draw a solid shaded primitive. */
        SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID = 0,
        /** @brief Draw only primitive wire edges. */
        SLAYER3D_GAME_DATA_RENDER_DRAW_WIRE = 1,
        /** @brief Draw a solid shaded primitive with wire edges overlaid. */
        SLAYER3D_GAME_DATA_RENDER_DRAW_SOLID_WIRE = 2,
    } slayer3d_game_data_render_draw_mode;

    /**
     * @brief Read-only descriptor for an authored render primitive component.
     *
     * This is an engine-data description, not a draw call. Renderers or demos
     * may interpret these descriptors to draw simple generic primitives while
     * preserving renderer ownership outside the game data runtime.
     */
    typedef struct slayer3d_game_data_render_primitive
    {
        /** @brief Name of the entity that owns the component. */
        const char *entity_name;
        /** @brief Primitive type declared by the component. */
        slayer3d_game_data_render_primitive_type type;
        /** @brief Procedural mesh kind for SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE. */
        slayer3d_game_data_mesh_primitive_kind mesh_primitive;
        /** @brief Draw mode for SLAYER3D_GAME_DATA_RENDER_MESH_PRIMITIVE. */
        slayer3d_game_data_render_draw_mode draw_mode;
        /** @brief Current world-space position from the owning actor plus optional component offset. */
        slayer3d_vec3 position;
        /** @brief True when @ref position is a camera-local offset instead of a world-space position. */
        bool view_space;
        /** @brief Optional world-space instance positions for batched primitives. */
        const slayer3d_vec3 *instances;
        /** @brief Number of entries in @ref instances for batched primitives. */
        int instance_count;
        /** @brief Axis used for primitive-local rotation by primitives that support rotation. */
        slayer3d_vec3 rotation_axis;
        /** @brief Primitive-local rotation angle in radians by primitives that support rotation. */
        float rotation_angle;
        /** @brief Euler rotation in radians for model primitives. */
        slayer3d_vec3 euler_rotation;
        /** @brief Cube size for SLAYER3D_GAME_DATA_RENDER_CUBE. */
        slayer3d_vec3 size;
        /** @brief Sphere radius for SLAYER3D_GAME_DATA_RENDER_SPHERE. */
        float radius;
        /** @brief Sphere longitudinal slices for SLAYER3D_GAME_DATA_RENDER_SPHERE. */
        int slices;
        /** @brief Sphere latitudinal rings for SLAYER3D_GAME_DATA_RENDER_SPHERE. */
        int rings;
        /** @brief Mesh primitive height for capsule, cylinder, cone, pyramid, and wedge primitives. */
        float height;
        /** @brief Mesh primitive top radius for cylinder/cone-style primitives. */
        float radius_top;
        /** @brief Mesh primitive bottom radius for cylinder/cone-style primitives. */
        float radius_bottom;
        /** @brief Torus major radius for SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS. */
        float major_radius;
        /** @brief Torus minor/tube radius for SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TORUS. */
        float minor_radius;
        /** @brief Rounded-box bevel radius for SLAYER3D_GAME_DATA_MESH_PRIMITIVE_ROUNDED_BOX. */
        float bevel_radius;
        /** @brief Arc angle in radians for SLAYER3D_GAME_DATA_MESH_PRIMITIVE_TUBE_SEGMENT. */
        float arc_angle;
        /** @brief Secondary tessellation count, such as torus tube segments. */
        int tube_segments;
        /** @brief Whether global procedural LOD may reduce tessellation for this primitive. */
        bool lod_enabled;
        /** @brief Multiplicative bias applied to projected size for procedural LOD selection. */
        float lod_bias;
        /** @brief Optional per-model projected-pixel cull threshold; negative uses render settings. */
        float lod_cull_pixels;
        /** @brief Authored tint color. */
        slayer3d_color color;
        /** @brief Optional wire overlay color for mesh primitive wire draw modes. */
        slayer3d_color wire_color;
        /** @brief Optional image asset id used as an albedo texture by primitives that support textures. */
        const char *texture_image;
        /** @brief Optional sprite asset id for SLAYER3D_GAME_DATA_RENDER_SPRITE. */
        const char *sprite_asset;
        /** @brief Optional model asset id for SLAYER3D_GAME_DATA_RENDER_MODEL. */
        const char *model_asset;
        /** @brief Model scale for SLAYER3D_GAME_DATA_RENDER_MODEL. */
        slayer3d_vec3 model_scale;
        /** @brief Animation clip index for SLAYER3D_GAME_DATA_RENDER_MODEL, or -1 for bind/static pose. */
        int animation_clip;
        /** @brief Animation time in seconds for SLAYER3D_GAME_DATA_RENDER_MODEL. */
        float animation_time;
        /** @brief Whether SLAYER3D_GAME_DATA_RENDER_MODEL wraps animation time by the clip duration. */
        bool animation_loop;
        /** @brief Billboard size for SLAYER3D_GAME_DATA_RENDER_SPRITE. */
        slayer3d_vec2 sprite_size;
        /** @brief World yaw in radians for directional sprite frame selection. */
        float sprite_facing_yaw;
        /** @brief True when the primitive should use scene lighting. */
        bool lighting_enabled;
        /** @brief Whether the primitive should be treated as emissive by the caller. */
        bool emissive;
        /** @brief Evaluated emissive RGB contribution. */
        slayer3d_vec3 emissive_color;
    } slayer3d_game_data_render_primitive;

    /**
     * @brief Callback for iterating authored render primitives.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_render_primitive_fn)(void *userdata,
                                                           const slayer3d_game_data_render_primitive *primitive);

    /** @brief Authored render setup that can be applied to a render context. */
    typedef struct slayer3d_game_data_render_settings
    {
        /** @brief Clear color for the frame. */
        slayer3d_color clear_color;
        /** @brief True when a render profile was authored or selected through scene state. */
        bool has_profile;
        /** @brief Render profile selected by authored data or scene state. */
        slayer3d_render_profile profile;
        /** @brief Name of the selected render profile, or NULL. */
        const char *profile_name;
        /** @brief Whether 3D lighting should be enabled. */
        bool lighting_enabled;
        /** @brief Whether bloom post-processing should be enabled. */
        bool bloom_enabled;
        /** @brief Whether SSAO post-processing should be enabled. */
        bool ssao_enabled;
        /** @brief Whether the GL backend should run an opaque depth pre-pass before lit geometry. */
        bool depth_prepass_enabled;
        /** @brief Whether lit draws should select only their most relevant lights from the scene candidate set. */
        bool per_object_light_selection_enabled;
        /** @brief Maximum lights uploaded to the shader for one lit draw. */
        int per_object_light_limit;
        /** @brief Whether procedural primitives may reduce tessellation based on projected screen size. */
        bool procedural_lod_enabled;
        /** @brief Projected diameter in pixels at or above which authored tessellation is preserved. */
        float procedural_lod_near_pixels;
        /** @brief Projected diameter in pixels at or below which minimum tessellation is selected. */
        float procedural_lod_far_pixels;
        /** @brief Minimum generated segment/ring count for procedural LOD. */
        int procedural_lod_min_segments;
        /** @brief Whether tiny model primitives may be culled by projected screen size. */
        bool model_lod_culling_enabled;
        /** @brief Projected model diameter at or below which model LOD culling skips the draw. */
        float model_lod_cull_pixels;
        /** @brief Whether capable backends should collect GPU sample-count diagnostics. */
        bool performance_queries_enabled;
        /** @brief Internal 3D/world render scale; UI and logical presentation remain full resolution. */
        float world_render_scale;
        /** @brief Tonemap operator for lit rendering. */
        slayer3d_tonemap_mode tonemap;
    } slayer3d_game_data_render_settings;

#ifdef __cplusplus
}
#endif

#endif

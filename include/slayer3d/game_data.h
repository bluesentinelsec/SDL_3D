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

    /**
     * @brief Exact byte size of an authored network control message packet.
     *
     * Control messages carry only fixed metadata: magic, version, tick,
     * control-message index, and the schema hash.
     */
#define SLAYER3D_GAME_DATA_NETWORK_CONTROL_PACKET_SIZE (16U + SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE)

    /** @brief Maximum bytes stored for one dynamic menu row label or value, including room for terminator. */
#define SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY 256U

    /** @brief Default vertical field-of-view for authored perspective, chase, and FPS cameras. */
#define SLAYER3D_GAME_DATA_DEFAULT_CAMERA_FOVY_DEGREES 90.0f

    /** @brief Default world unit label. SLAYER3D convention is one authored world unit equals one meter. */
#define SLAYER3D_GAME_DATA_DEFAULT_WORLD_UNITS "meters"

    /** @brief Default scale from authored world units to real-world meters. */
#define SLAYER3D_GAME_DATA_DEFAULT_METERS_PER_UNIT 1.0f

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /** @brief Authored application lifecycle hooks. */
    typedef struct slayer3d_game_data_app_control
    {
        /** @brief Signal emitted by the host after game data has loaded, or -1. */
        int start_signal_id;
        /** @brief Input action that requests app quit, or -1. */
        int quit_action_id;
        /** @brief Input action that requests pause/unpause, or -1. */
        int pause_action_id;
        /** @brief Transition name to play at startup, or NULL. */
        const char *startup_transition;
        /** @brief Transition name to play before quit, or NULL. */
        const char *quit_transition;
        /** @brief Signal that means the app should quit immediately, or -1. */
        int quit_signal_id;
        /** @brief Signal that applies live window settings, or -1. */
        int window_apply_signal_id;
        /** @brief Actor containing authored window setting properties, or NULL. */
        const char *window_settings_target;
        /** @brief Property key containing display mode, or NULL. */
        const char *window_display_mode_key;
        /** @brief Property key containing renderer/backend, or NULL. */
        const char *window_renderer_key;
        /** @brief Property key containing V-sync, or NULL. */
        const char *window_vsync_key;
    } slayer3d_game_data_app_control;

    /** @brief Direction for an authored network replication or control message. */
    typedef enum slayer3d_game_data_network_direction
    {
        /** @brief Invalid or unknown network direction. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_INVALID = 0,
        /** @brief Message flows from the authoritative host to a client. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_HOST_TO_CLIENT = 1,
        /** @brief Message flows from a client to the authoritative host. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_CLIENT_TO_HOST = 2,
        /** @brief Message may flow in either direction. */
        SLAYER3D_GAME_DATA_NETWORK_DIRECTION_BIDIRECTIONAL = 3,
    } slayer3d_game_data_network_direction;

    /** @brief Decoded authored network control message descriptor. */
    typedef struct slayer3d_game_data_network_control
    {
        /** @brief Authored control message name, owned by the runtime. */
        const char *name;
        /** @brief Authored message direction. */
        slayer3d_game_data_network_direction direction;
        /** @brief Signal referenced by the control message, or -1. */
        int signal_id;
        /** @brief Tick carried by the control packet. */
        Uint32 tick;
    } slayer3d_game_data_network_control;

    /** @brief Authored haptics/rumble policy selected by a signal payload. */
    typedef struct slayer3d_game_data_haptics_policy
    {
        /** @brief Stable authored policy name, owned by the runtime. */
        const char *name;
        /** @brief Signal that triggers the policy, or -1. */
        int signal_id;
        /** @brief Low-frequency rumble intensity in the range [0, 1]. */
        float low_frequency;
        /** @brief High-frequency rumble intensity in the range [0, 1]. */
        float high_frequency;
        /** @brief Rumble duration in milliseconds. */
        Uint32 duration_ms;
    } slayer3d_game_data_haptics_policy;

    /** @brief Authored font asset descriptor. */
    typedef struct slayer3d_game_data_font_asset
    {
        /** @brief Stable asset id, such as `font.hud`. */
        const char *id;
        /** @brief Built-in font id when @p builtin is true. */
        slayer3d_builtin_font builtin_id;
        /** @brief True when this asset refers to an SLAYER3D built-in font. */
        bool builtin;
        /** @brief External font path when @p builtin is false, or NULL. */
        const char *path;
        /** @brief Requested font pixel size. */
        float size;
    } slayer3d_game_data_font_asset;

    /** @brief Authored image asset descriptor. */
    typedef struct slayer3d_game_data_image_asset
    {
        /** @brief Stable asset id, such as `image.logo`. */
        const char *id;
        /** @brief Virtual or filesystem path to the image bytes, or NULL. */
        const char *path;
        /** @brief Optional sprite asset id when the image is sprite-backed. */
        const char *sprite;
    } slayer3d_game_data_image_asset;

    /** @brief Authored 3D model asset descriptor. */
    typedef struct slayer3d_game_data_model_asset
    {
        /** @brief Stable asset id, such as `model.dragon`. */
        const char *id;
        /** @brief Virtual or filesystem path to the model source. */
        const char *path;
    } slayer3d_game_data_model_asset;

    /** @brief Authored scene skybox descriptor using six image asset ids. */
    typedef struct slayer3d_game_data_scene_skybox
    {
        /** @brief +X face image asset id. */
        const char *pos_x;
        /** @brief -X face image asset id. */
        const char *neg_x;
        /** @brief +Y face image asset id. */
        const char *pos_y;
        /** @brief -Y face image asset id. */
        const char *neg_y;
        /** @brief +Z face image asset id. */
        const char *pos_z;
        /** @brief -Z face image asset id. */
        const char *neg_z;
        /** @brief Skybox cube half-size in world units. */
        float size;
    } slayer3d_game_data_scene_skybox;

    /** @brief Authored sound-effect asset descriptor. */
    typedef struct slayer3d_game_data_sound_asset
    {
        /** @brief Stable asset id, such as `sound.ui.select`. */
        const char *id;
        /** @brief Virtual or filesystem path to the sound bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Default playback pitch. */
        float pitch;
        /** @brief Default stereo pan in [-1, 1]. */
        float pan;
        /** @brief Logical mix bus used by default. */
        slayer3d_audio_bus bus;
    } slayer3d_game_data_sound_asset;

    /** @brief Authored music asset descriptor. */
    typedef struct slayer3d_game_data_music_asset
    {
        /** @brief Stable asset id, such as `music.title`. */
        const char *id;
        /** @brief Virtual or filesystem path to the stream bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Whether playback should loop by default. */
        bool loop;
    } slayer3d_game_data_music_asset;

    /** @brief Authored ambient-zone asset descriptor. */
    typedef struct slayer3d_game_data_ambient_asset
    {
        /** @brief Stable asset id, such as `ambient.sewer.loop`. */
        const char *id;
        /** @brief Non-negative ambient zone id used by sector payloads. */
        int ambient_id;
        /** @brief Virtual or filesystem path to the ambient stream bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Whether playback should loop by default. */
        bool loop;
    } slayer3d_game_data_ambient_asset;

    /** @brief Authored sprite asset descriptor. */
    typedef struct slayer3d_game_data_sprite_asset
    {
        /** @brief Stable asset id, such as `sprite.robot.walk`. */
        const char *id;
        /** @brief Source kind: sheet image or explicit file list. */
        slayer3d_sprite_asset_source_kind source_kind;
        /** @brief Virtual or filesystem path to the sprite source image. */
        const char *path;
        /** @brief Optional vertex shader source path for a sprite-specific GPU program. */
        const char *shader_vertex_path;
        /** @brief Optional fragment shader source path for a sprite-specific GPU program. */
        const char *shader_fragment_path;
        /** @brief Frame width in pixels for a grid or atlas source. */
        int frame_width;
        /** @brief Frame height in pixels for a grid or atlas source. */
        int frame_height;
        /** @brief Number of frames across the source image. */
        int columns;
        /** @brief Number of rows across the source image. */
        int rows;
        /** @brief Number of animation frames in the authored source. */
        int frame_count;
        /** @brief Number of directional frames per animation frame. */
        int direction_count;
        /** @brief Playback rate in frames per second. */
        float fps;
        /** @brief Whether playback wraps after the last frame. */
        bool loop;
        /** @brief Whether the sprite participates in dynamic lighting. */
        bool lighting;
        /** @brief Whether the sprite is emissive. */
        bool emissive;
        /** @brief Offset from logical feet/contact point to visible feet. */
        float visual_ground_offset;
        /** @brief Optional sprite overlay effect id, such as `melt`. */
        const char *effect;
        /** @brief Delay before the effect begins, in presentation seconds. */
        float effect_delay;
        /** @brief Duration of the effect ramp, in seconds. */
        float effect_duration;
    } slayer3d_game_data_sprite_asset;

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
        /** @brief Authored editor work-plane grid line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_WORK_PLANE_GRID = 7,
        /** @brief Editor-authored player-start marker line. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_PLAYER_START_EDGE = 8,
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
        /** @brief Emit authored editor work-plane grid lines. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID = 1u << 6,
        /** @brief Emit editor-authored player-start marker icons. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS = 1u << 7,
        /** @brief Emit every supported editor debug primitive. */
        SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_ALL =
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORLD_BOUNDS | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_SELECTION_BOUNDS |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_TRACE_RAY | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_FACE_NORMAL |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_HIT_MARKER | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_COMMAND_PREVIEW |
            SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_WORK_PLANE_GRID | SLAYER3D_GAME_DATA_EDITOR_DEBUG_DRAW_PLAYER_STARTS,
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
        /** @brief Color for trace rays, or alpha 0 for default. */
        slayer3d_color trace_color;
        /** @brief Color for face normals, or alpha 0 for default. */
        slayer3d_color face_normal_color;
        /** @brief Color for hit markers, or alpha 0 for default. */
        slayer3d_color hit_marker_color;
        /** @brief Color for command preview bounds, or alpha 0 for default. */
        slayer3d_color command_preview_color;
        /** @brief Color for work-plane grid lines, or alpha 0 for default. */
        slayer3d_color work_plane_grid_color;
        /** @brief Color for editor player-start marker lines, or alpha 0 for default. */
        slayer3d_color player_start_color;
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

    /**
     * @brief Callback for active authored sector level instances.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_sector_level_instance_fn)(
        void *userdata, const slayer3d_game_data_sector_level_instance *instance);

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

    /** @brief Horizontal UI alignment for authored text and generated menu items. */
    typedef enum slayer3d_game_data_ui_align
    {
        /** @brief Anchor text at its left edge. */
        SLAYER3D_GAME_DATA_UI_ALIGN_LEFT = 0,
        /** @brief Anchor text at its center. */
        SLAYER3D_GAME_DATA_UI_ALIGN_CENTER = 1,
        /** @brief Anchor text at its right edge. */
        SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT = 2,
    } slayer3d_game_data_ui_align;

    /** @brief Vertical UI alignment for authored images. */
    typedef enum slayer3d_game_data_ui_valign
    {
        /** @brief Anchor at the top edge. */
        SLAYER3D_GAME_DATA_UI_VALIGN_TOP = 0,
        /** @brief Anchor at the center. */
        SLAYER3D_GAME_DATA_UI_VALIGN_CENTER = 1,
        /** @brief Anchor at the bottom edge. */
        SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM = 2,
    } slayer3d_game_data_ui_valign;

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

    /** @brief Authored transition effect descriptor. */
    typedef struct slayer3d_game_data_transition_desc
    {
        /** @brief Transition effect type. */
        slayer3d_transition_type type;
        /** @brief Transition direction. */
        slayer3d_transition_direction direction;
        /** @brief Transition color. */
        slayer3d_color color;
        /** @brief Duration in seconds. */
        float duration;
        /** @brief Signal emitted on completion, or -1. */
        int done_signal_id;
    } slayer3d_game_data_transition_desc;

    /** @brief Authored UI text descriptor. */
    typedef struct slayer3d_game_data_ui_text
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Font asset id. */
        const char *font;
        /** @brief Literal text, or NULL when @p format is used. */
        const char *text;
        /** @brief Format string interpreted by the caller. */
        const char *format;
        /** @brief Caller-defined source key for dynamic text. */
        const char *source;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal position. For centered text, this is a normalized y-independent coordinate. */
        float x;
        /** @brief Vertical position. */
        float y;
        /** @brief Whether x/y are normalized to the current render size. */
        bool normalized;
        /** @brief Whether the text should be horizontally centered by the caller. */
        bool centered;
        /** @brief Horizontal alignment used by richer UI layouts. */
        slayer3d_game_data_ui_align align;
        /** @brief Runtime or authored scale multiplier applied during presentation. */
        float scale;
        /** @brief Whether alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Text color. */
        slayer3d_color color;
    } slayer3d_game_data_ui_text;

    /** @brief Authored UI image descriptor. */
    typedef struct slayer3d_game_data_ui_image
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Image asset id. */
        const char *image;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal anchor position. */
        float x;
        /** @brief Vertical anchor position. */
        float y;
        /** @brief Desired width. */
        float w;
        /** @brief Desired height. */
        float h;
        /** @brief Whether x/y/w/h are normalized to the current render size. */
        bool normalized;
        /** @brief Whether to preserve the source image aspect ratio inside w/h. */
        bool preserve_aspect;
        /** @brief Horizontal alignment of the image rectangle around x. */
        slayer3d_game_data_ui_align align;
        /** @brief Vertical alignment of the image rectangle around y. */
        slayer3d_game_data_ui_valign valign;
        /** @brief Runtime or authored scale multiplier applied around the image anchor. */
        float scale;
        /** @brief Image tint color. */
        slayer3d_color color;
        /** @brief Optional UI image effect id, such as `melt`. */
        const char *effect;
        /** @brief Effect progression speed in effect-seconds per second. */
        float effect_speed;
    } slayer3d_game_data_ui_image;

    /** @brief Authored UI rectangle descriptor. */
    typedef struct slayer3d_game_data_ui_rect
    {
        /** @brief Stable UI item name. */
        const char *name;
        /** @brief Caller-defined visibility key. */
        const char *visible;
        /** @brief Horizontal position. */
        float x;
        /** @brief Vertical position. */
        float y;
        /** @brief Rectangle width. */
        float w;
        /** @brief Rectangle height. */
        float h;
        /** @brief Whether x/y/w/h are normalized to the current render size. */
        bool normalized;
        /** @brief Horizontal alignment of the rectangle around x. */
        slayer3d_game_data_ui_align align;
        /** @brief Vertical alignment of the rectangle around y. */
        slayer3d_game_data_ui_valign valign;
        /** @brief Runtime or authored scale multiplier applied around the rectangle anchor. */
        float scale;
        /** @brief Rectangle color. */
        slayer3d_color color;
        /** @brief Optional actor that supplies an alpha multiplier property. */
        const char *alpha_source_target;
        /** @brief Optional property on @ref alpha_source_target used as alpha source. */
        const char *alpha_source_key;
        /** @brief Multiplier applied to the alpha source property. */
        float alpha_source_scale;
        /** @brief Minimum alpha multiplier when an alpha source is authored. */
        float alpha_source_min;
        /** @brief Maximum alpha multiplier when an alpha source is authored. */
        float alpha_source_max;
        /** @brief True when alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Pulse frequency in cycles per second. */
        float pulse_rate;
        /** @brief Minimum pulse alpha multiplier. */
        float pulse_min;
        /** @brief Maximum pulse alpha multiplier. */
        float pulse_max;
    } slayer3d_game_data_ui_rect;

    /** @brief Bit flags indicating which runtime UI state fields override authored descriptor values. */
    typedef enum slayer3d_game_data_ui_state_flags
    {
        /** @brief Override UI visibility. */
        SLAYER3D_GAME_DATA_UI_STATE_VISIBLE = 1u << 0,
        /** @brief Add a runtime x/y offset to the authored UI position. */
        SLAYER3D_GAME_DATA_UI_STATE_OFFSET = 1u << 1,
        /** @brief Multiply the authored UI scale. */
        SLAYER3D_GAME_DATA_UI_STATE_SCALE = 1u << 2,
        /** @brief Multiply the authored UI alpha. */
        SLAYER3D_GAME_DATA_UI_STATE_ALPHA = 1u << 3,
        /** @brief Multiply the authored UI tint/color. */
        SLAYER3D_GAME_DATA_UI_STATE_TINT = 1u << 4,
    } slayer3d_game_data_ui_state_flags;

    /**
     * @brief Runtime presentation state for an authored UI item.
     *
     * Runtime state is keyed by the UI item's authored `name`. It is layered on
     * top of static JSON descriptors during resolution, which lets timelines,
     * scripts, or host code animate UI elements without mutating game data.
     */
    typedef struct slayer3d_game_data_ui_state
    {
        /** @brief Combination of slayer3d_game_data_ui_state_flags values. */
        Uint32 flags;
        /** @brief Visibility override used when SLAYER3D_GAME_DATA_UI_STATE_VISIBLE is set. */
        bool visible;
        /** @brief Runtime x offset in the descriptor's coordinate space. */
        float offset_x;
        /** @brief Runtime y offset in the descriptor's coordinate space. */
        float offset_y;
        /** @brief Scale multiplier used when SLAYER3D_GAME_DATA_UI_STATE_SCALE is set. */
        float scale;
        /** @brief Alpha multiplier in [0, 1] used when SLAYER3D_GAME_DATA_UI_STATE_ALPHA is set. */
        float alpha;
        /** @brief Tint multiplier used when SLAYER3D_GAME_DATA_UI_STATE_TINT is set. */
        slayer3d_color tint;
    } slayer3d_game_data_ui_state;

    /**
     * @brief Callback for iterating authored UI text descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_text_fn)(void *userdata, const slayer3d_game_data_ui_text *text);

    /**
     * @brief Callback for iterating authored UI image descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_image_fn)(void *userdata, const slayer3d_game_data_ui_image *image);

    /**
     * @brief Callback for iterating authored UI rectangle descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_ui_rect_fn)(void *userdata, const slayer3d_game_data_ui_rect *rect);

    /** @brief Input mode used by a data-authored scene skip policy. */
    typedef enum slayer3d_game_data_skip_input
    {
        /** @brief The active scene cannot be skipped by input. */
        SLAYER3D_GAME_DATA_SKIP_INPUT_DISABLED = 0,
        /** @brief Any key, pointer, or gamepad press skips the scene. */
        SLAYER3D_GAME_DATA_SKIP_INPUT_ANY = 1,
        /** @brief A specific authored input action skips the scene. */
        SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION = 2,
    } slayer3d_game_data_skip_input;

    /**
     * @brief Data-authored input policy for skipping the active scene.
     *
     * Skip policies are generic scene-flow primitives. They are appropriate
     * for splash screens, cutscenes, attract modes, and any scene whose author
     * wants controlled early advancement without hard-coding scene-specific
     * input handling.
     */
    typedef struct slayer3d_game_data_skip_policy
    {
        /** @brief True when this policy should be evaluated. */
        bool enabled;
        /** @brief Input source that can trigger the skip. */
        slayer3d_game_data_skip_input input;
        /** @brief Authored input action name when @p input is SLAYER3D_GAME_DATA_SKIP_INPUT_ACTION. */
        const char *action;
        /** @brief Resolved action id for @p action, or -1. */
        int action_id;
        /** @brief Target scene requested when the policy triggers. */
        const char *scene;
        /** @brief True to route the request through the active scene's exit transition. */
        bool preserve_exit_transition;
        /** @brief True to suppress other app/menu controls for the triggering frame. */
        bool consume_input;
        /** @brief True to suppress active-scene menus when skip input triggers. */
        bool block_menus;
        /** @brief True to suppress authored scene shortcuts when skip input triggers. */
        bool block_scene_shortcuts;
    } slayer3d_game_data_skip_policy;

    /**
     * @brief Interaction policy for an active scene's autoplay timeline.
     *
     * These flags let intro, splash, attract, and cutscene authors decide
     * whether a still-running timeline owns scene flow or whether normal menus
     * and scene shortcuts remain interactive while timed events continue.
     */
    typedef struct slayer3d_game_data_timeline_policy
    {
        /** @brief True while an incomplete autoplay timeline suppresses active-scene menus. */
        bool block_menus;
        /** @brief True while an incomplete autoplay timeline suppresses authored scene shortcuts. */
        bool block_scene_shortcuts;
    } slayer3d_game_data_timeline_policy;

    /**
     * @brief Runtime state for a data-authored active-scene timeline.
     *
     * Hosts keep this state across frames. The game data runtime resets it
     * automatically when the active scene changes.
     */
    typedef struct slayer3d_game_data_timeline_state
    {
        /** @brief Runtime-owned active scene pointer currently tracked by this state. */
        const char *scene;
        /** @brief Elapsed timeline time in seconds for @p scene. */
        float time;
        /** @brief Next authored event index to evaluate. */
        int next_event_index;
        /** @brief True once all authored events have fired. */
        bool complete;
    } slayer3d_game_data_timeline_state;

    /**
     * @brief Result produced after advancing an active-scene timeline.
     */
    typedef struct slayer3d_game_data_timeline_update_result
    {
        /** @brief Scene requested by a `scene.request` timeline action, or NULL. */
        const char *scene_request;
        /** @brief Number of timeline actions executed during this update. */
        int actions_executed;
        /** @brief True when the active timeline has no more events to fire. */
        bool complete;
    } slayer3d_game_data_timeline_update_result;

    /**
     * @brief Runtime descriptor for the active scene's primary menu.
     *
     * Menus are authored in scene JSON files and map input actions to a
     * selected item. The runtime owns all string pointers.
     */
    typedef struct slayer3d_game_data_menu
    {
        /** @brief Stable menu name. */
        const char *name;
        /** @brief Input action that moves selection up, or -1. */
        int up_action_id;
        /** @brief Input action that moves selection down, or -1. */
        int down_action_id;
        /** @brief Input action that decreases the selected control, or -1. */
        int left_action_id;
        /** @brief Input action that increases the selected control, or -1. */
        int right_action_id;
        /** @brief Input action that activates the selected item, or -1. */
        int select_action_id;
        /** @brief Input action that activates the menu's back item, or -1. */
        int back_action_id;
        /** @brief Signal emitted after successful navigation, or -1. */
        int move_signal_id;
        /** @brief Signal emitted when the selected item is activated, or -1. */
        int select_signal_id;
        /** @brief Currently selected zero-based item index. */
        int selected_index;
        /** @brief Number of selectable menu items. */
        int item_count;
    } slayer3d_game_data_menu;

    /** @brief Generic data-authored control kind for menu items. */
    typedef enum slayer3d_game_data_menu_control_type
    {
        /** @brief Menu item is a command, not a setting control. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_NONE = 0,
        /** @brief Menu item toggles a boolean actor property. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_TOGGLE = 1,
        /** @brief Menu item cycles through authored choices. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_CHOICE = 2,
        /** @brief Menu item increments a numeric property within a range. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_RANGE = 3,
        /** @brief Menu item captures a keyboard key or gamepad button and rebinds authored actions. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_INPUT_BINDING = 4,
        /** @brief Menu item captures editable text and writes it to scene state or an actor property. */
        SLAYER3D_GAME_DATA_MENU_CONTROL_TEXT = 5,
    } slayer3d_game_data_menu_control_type;

    /** @brief App pause command authored on a menu item. */
    typedef enum slayer3d_game_data_menu_pause_command
    {
        /** @brief Selecting the item does not change pause state. */
        SLAYER3D_GAME_DATA_MENU_PAUSE_NONE = 0,
        /** @brief Selecting the item pauses the app. */
        SLAYER3D_GAME_DATA_MENU_PAUSE_PAUSE = 1,
        /** @brief Selecting the item resumes the app. */
        SLAYER3D_GAME_DATA_MENU_PAUSE_RESUME = 2,
        /** @brief Selecting the item toggles the app pause state. */
        SLAYER3D_GAME_DATA_MENU_PAUSE_TOGGLE = 3,
    } slayer3d_game_data_menu_pause_command;

    /** @brief Result of advancing an active input-binding capture. */
    typedef enum slayer3d_game_data_input_binding_capture_status
    {
        /** @brief No input-binding capture is active. */
        SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_NONE = 0,
        /** @brief Capture is active and still waiting for an input press. */
        SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_WAITING = 1,
        /** @brief Capture was canceled by its cancel input. */
        SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CANCELED = 2,
        /** @brief The captured input was applied to authored action bindings. */
        SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CHANGED = 3,
        /** @brief The captured input was rejected because another binding uses it. */
        SLAYER3D_GAME_DATA_INPUT_BINDING_CAPTURE_CONFLICT = 4,
    } slayer3d_game_data_input_binding_capture_status;

    /** @brief Result of advancing an active menu text-entry capture. */
    typedef enum slayer3d_game_data_text_entry_capture_status
    {
        /** @brief No text-entry capture is active. */
        SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_NONE = 0,
        /** @brief Capture is active and waiting for more input. */
        SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_WAITING = 1,
        /** @brief Capture was canceled and the original value was restored. */
        SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CANCELED = 2,
        /** @brief Capture is active and edited the bound value. */
        SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_CHANGED = 3,
        /** @brief Capture was submitted. */
        SLAYER3D_GAME_DATA_TEXT_ENTRY_CAPTURE_SUBMITTED = 4,
    } slayer3d_game_data_text_entry_capture_status;

    /**
     * @brief Runtime descriptor for one authored menu item.
     *
     * A menu item may request a scene change, request app quit, emit a signal,
     * change pause state, return to a previously authored scene, or mutate an
     * actor property as a generic option control. Hosts can use these fields
     * directly or translate them into a higher-level scene transition flow.
     */
    typedef struct slayer3d_game_data_menu_item
    {
        /** @brief Display label for the item. */
        const char *label;
        /** @brief Target scene name, or NULL when this item does not change scene. */
        const char *scene;
        /** @brief Scene stored as the return target when this item changes scene, or NULL. */
        const char *return_to;
        /** @brief Scene-state key to set when this item is selected, or NULL. */
        const char *scene_state_key;
        /** @brief String value assigned to scene_state_key when this item is selected, or NULL. */
        const char *scene_state_value;
        /** @brief True when selecting this item requests the stored return scene. */
        bool return_scene;
        /** @brief True when selecting this item requests application quit. */
        bool quit;
        /** @brief Signal emitted by this item, or -1 when not authored. */
        int signal_id;
        /** @brief Authored pause command to apply when selecting this item. */
        slayer3d_game_data_menu_pause_command pause_command;
        /** @brief True when selecting this item stores a return pause state. */
        bool has_return_paused;
        /** @brief Pause state stored for a later return_scene item. */
        bool return_paused;
        /** @brief Authored generic control type. */
        slayer3d_game_data_menu_control_type control_type;
        /** @brief Actor that owns the controlled property, or NULL. */
        const char *control_target;
        /** @brief Controlled property key, or NULL. */
        const char *control_key;
        /** @brief Number of authored choices for choice controls. */
        int choice_count;
        /** @brief Number of action bindings affected by an input-binding control. */
        int input_binding_count;
        /** @brief True when this item was expanded from an authored dynamic list. */
        bool dynamic_list_item;
        /** @brief Authored dynamic list name, or NULL for static menu items. */
        const char *dynamic_list_name;
        /** @brief Zero-based row index inside the dynamic list, or -1 for static/empty rows. */
        int dynamic_list_index;
        /** @brief Runtime-owned value associated with the dynamic row, or NULL. */
        const char *dynamic_list_value;
        /** @brief Storage backing label for dynamic-list rows. */
        char dynamic_list_label_storage[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
        /** @brief Storage backing scene_state_value for dynamic-list rows. */
        char dynamic_list_scene_state_value_storage[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
        /** @brief Storage backing dynamic_list_value. */
        char dynamic_list_value_storage[SLAYER3D_GAME_DATA_MENU_DYNAMIC_TEXT_CAPACITY];
    } slayer3d_game_data_menu_item;

    /** @brief Authored scene transition behavior policy. */
    typedef struct slayer3d_game_data_scene_transition_policy
    {
        /** @brief Permit requesting the currently active scene. */
        bool allow_same_scene;
        /** @brief Permit a new scene request to replace an active transition. */
        bool allow_interrupt;
        /** @brief Reset menu input arming after an accepted scene request. */
        bool reset_menu_input_on_request;
    } slayer3d_game_data_scene_transition_policy;

    /** @brief Authored input shortcut that requests a scene change. */
    typedef struct slayer3d_game_data_scene_shortcut
    {
        /** @brief Input action id resolved from the authored action name, or -1. */
        int action_id;
        /** @brief Authored input action name. */
        const char *action;
        /** @brief Target scene name. */
        const char *scene;
    } slayer3d_game_data_scene_shortcut;

    /** @brief Read-only descriptor for an authored particle emitter component. */
    typedef struct slayer3d_game_data_particle_emitter
    {
        /** @brief Name of the entity that owns the emitter. */
        const char *entity_name;
        /** @brief Emitter configuration evaluated from authored data and actor position. */
        slayer3d_particle_config config;
        /** @brief True when particle positions are evaluated in camera/viewmodel space. */
        bool view_space;
        /** @brief Draw-time emissive color to apply around particle rendering. */
        slayer3d_vec3 draw_emissive;
    } slayer3d_game_data_particle_emitter;

    /**
     * @brief Callback for iterating active authored particle emitters.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*slayer3d_game_data_particle_emitter_fn)(void *userdata,
                                                           const slayer3d_game_data_particle_emitter *emitter);

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
     * material, and face. Pointers remain valid until the runtime is destroyed.
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
     * remain valid until the runtime is destroyed or reloaded.
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
     * increments its editor revision. @p out_brush_name receives the final
     * runtime brush name when non-NULL.
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
     * hash, render mesh totals, spatial chunk metadata, and visibility-grid
     * metadata. This is an inspection and cache-invalidation descriptor; it does
     * not contain the binary mesh or collision payloads needed to load a compiled
     * artifact directly. The returned string is allocated with SDL_malloc and
     * must be released with SDL_free().
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
     * offline artifact is reusable. Stale manifests are reported through
     * @p out_status rather than treated as API errors.
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
     * @brief Export one editable level fragment containing brushes and starts.
     *
     * The exported document uses `schema: "slayer3d.fragment.v0"` and contains
     * the selected `brush_worlds` entry plus the runtime `editor_player_starts`
     * collection. This is the canonical first-pass editor level artifact for
     * blockout workflows that need geometry and test-run spawn points to reload
     * together. The returned string is allocated with SDL_malloc and must be
     * released with SDL_free().
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
     * @brief Load an editable level fragment file into an existing editor runtime.
     *
     * The input must be a `slayer3d.fragment.v0` document containing a
     * `brush_worlds` entry whose name matches @p world_name. The matching world
     * replaces the runtime world in place, and `editor_player_starts` from the
     * fragment replaces the runtime player-start collection. On success both
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
     * context to launch the generic runner without game-specific native code.
     * The returned string is allocated with SDL_malloc and must be released
     * with SDL_free().
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

/**
 * @file game_data.h
 * @brief JSON-authored game data runtime.
 *
 * The game data runtime loads an SDL3D game JSON file and instantiates its
 * generic composition primitives into an existing game session: actors, input
 * actions, signals, timers, sensors, and signal-to-action bindings.
 *
 * Game-specific behavior stays behind named adapters. JSON chooses where an
 * adapter is invoked and can bind it to a Lua function loaded from a script
 * next to the data file. Game code may also register native C callbacks for
 * adapters that need host integration or optimized native code.
 */

#ifndef SDL3D_GAME_DATA_H
#define SDL3D_GAME_DATA_H

#include <stdbool.h>

#include "sdl3d/actor_registry.h"
#include "sdl3d/asset.h"
#include "sdl3d/camera.h"
#include "sdl3d/effects.h"
#include "sdl3d/font.h"
#include "sdl3d/game.h"
#include "sdl3d/lighting.h"
#include "sdl3d/properties.h"
#include "sdl3d/storage.h"
#include "sdl3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct sdl3d_game_data_runtime sdl3d_game_data_runtime;

    /** @brief Authored application lifecycle hooks. */
    typedef struct sdl3d_game_data_app_control
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
    } sdl3d_game_data_app_control;

    /** @brief Authored font asset descriptor. */
    typedef struct sdl3d_game_data_font_asset
    {
        /** @brief Stable asset id, such as `font.hud`. */
        const char *id;
        /** @brief Built-in font id when @p builtin is true. */
        sdl3d_builtin_font builtin_id;
        /** @brief True when this asset refers to an SDL3D built-in font. */
        bool builtin;
        /** @brief External font path when @p builtin is false, or NULL. */
        const char *path;
        /** @brief Requested font pixel size. */
        float size;
    } sdl3d_game_data_font_asset;

    /** @brief Authored image asset descriptor. */
    typedef struct sdl3d_game_data_image_asset
    {
        /** @brief Stable asset id, such as `image.logo`. */
        const char *id;
        /** @brief Virtual or filesystem path to the image bytes. */
        const char *path;
    } sdl3d_game_data_image_asset;

    /** @brief Authored sound-effect asset descriptor. */
    typedef struct sdl3d_game_data_sound_asset
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
        sdl3d_audio_bus bus;
    } sdl3d_game_data_sound_asset;

    /** @brief Authored music asset descriptor. */
    typedef struct sdl3d_game_data_music_asset
    {
        /** @brief Stable asset id, such as `music.title`. */
        const char *id;
        /** @brief Virtual or filesystem path to the stream bytes. */
        const char *path;
        /** @brief Default authored gain before bus volume. */
        float volume;
        /** @brief Whether playback should loop by default. */
        bool loop;
    } sdl3d_game_data_music_asset;

    /**
     * @brief Runtime metrics used when evaluating data-authored UI bindings.
     *
     * Games provide this small host-state snapshot each frame. The game data
     * runtime combines it with actor properties and active camera state to
     * resolve UI visibility and text content without game-specific string maps.
     */
    typedef struct sdl3d_game_data_ui_metrics
    {
        /** @brief Whether the managed loop is currently paused. */
        bool paused;
        /** @brief Most recently sampled frames per second. */
        float fps;
        /** @brief Number of rendered frames. */
        Uint64 frame;
    } sdl3d_game_data_ui_metrics;

    /** @brief Optional render evaluation inputs for dynamic visual effects. */
    typedef struct sdl3d_game_data_render_eval
    {
        /** @brief Elapsed presentation time in seconds, used by pulse effects. */
        float time;
    } sdl3d_game_data_render_eval;

    /** @brief Horizontal UI alignment for authored text and generated menu items. */
    typedef enum sdl3d_game_data_ui_align
    {
        /** @brief Anchor text at its left edge. */
        SDL3D_GAME_DATA_UI_ALIGN_LEFT = 0,
        /** @brief Anchor text at its center. */
        SDL3D_GAME_DATA_UI_ALIGN_CENTER = 1,
        /** @brief Anchor text at its right edge. */
        SDL3D_GAME_DATA_UI_ALIGN_RIGHT = 2,
    } sdl3d_game_data_ui_align;

    /** @brief Vertical UI alignment for authored images. */
    typedef enum sdl3d_game_data_ui_valign
    {
        /** @brief Anchor at the top edge. */
        SDL3D_GAME_DATA_UI_VALIGN_TOP = 0,
        /** @brief Anchor at the center. */
        SDL3D_GAME_DATA_UI_VALIGN_CENTER = 1,
        /** @brief Anchor at the bottom edge. */
        SDL3D_GAME_DATA_UI_VALIGN_BOTTOM = 2,
    } sdl3d_game_data_ui_valign;

    /** @brief Authored render primitive kind. */
    typedef enum sdl3d_game_data_render_primitive_type
    {
        /** @brief Axis-aligned cube/box primitive. */
        SDL3D_GAME_DATA_RENDER_CUBE = 1,
        /** @brief Sphere primitive. */
        SDL3D_GAME_DATA_RENDER_SPHERE = 2,
    } sdl3d_game_data_render_primitive_type;

    /**
     * @brief Read-only descriptor for an authored render primitive component.
     *
     * This is an engine-data description, not a draw call. Renderers or demos
     * may interpret these descriptors to draw simple generic primitives while
     * preserving renderer ownership outside the game data runtime.
     */
    typedef struct sdl3d_game_data_render_primitive
    {
        /** @brief Name of the entity that owns the component. */
        const char *entity_name;
        /** @brief Primitive type declared by the component. */
        sdl3d_game_data_render_primitive_type type;
        /** @brief Current world-space position from the owning actor plus optional component offset. */
        sdl3d_vec3 position;
        /** @brief Cube size for SDL3D_GAME_DATA_RENDER_CUBE. */
        sdl3d_vec3 size;
        /** @brief Sphere radius for SDL3D_GAME_DATA_RENDER_SPHERE. */
        float radius;
        /** @brief Sphere longitudinal slices for SDL3D_GAME_DATA_RENDER_SPHERE. */
        int slices;
        /** @brief Sphere latitudinal rings for SDL3D_GAME_DATA_RENDER_SPHERE. */
        int rings;
        /** @brief Authored tint color. */
        sdl3d_color color;
        /** @brief Whether the primitive should be treated as emissive by the caller. */
        bool emissive;
        /** @brief Evaluated emissive RGB contribution. */
        sdl3d_vec3 emissive_color;
    } sdl3d_game_data_render_primitive;

    /**
     * @brief Callback for iterating authored render primitives.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_render_primitive_fn)(void *userdata,
                                                        const sdl3d_game_data_render_primitive *primitive);

    /** @brief Authored render setup that can be applied to a render context. */
    typedef struct sdl3d_game_data_render_settings
    {
        /** @brief Clear color for the frame. */
        sdl3d_color clear_color;
        /** @brief Whether 3D lighting should be enabled. */
        bool lighting_enabled;
        /** @brief Whether bloom post-processing should be enabled. */
        bool bloom_enabled;
        /** @brief Whether SSAO post-processing should be enabled. */
        bool ssao_enabled;
        /** @brief Tonemap operator for lit rendering. */
        sdl3d_tonemap_mode tonemap;
    } sdl3d_game_data_render_settings;

    /** @brief Authored transition effect descriptor. */
    typedef struct sdl3d_game_data_transition_desc
    {
        /** @brief Transition effect type. */
        sdl3d_transition_type type;
        /** @brief Transition direction. */
        sdl3d_transition_direction direction;
        /** @brief Transition color. */
        sdl3d_color color;
        /** @brief Duration in seconds. */
        float duration;
        /** @brief Signal emitted on completion, or -1. */
        int done_signal_id;
    } sdl3d_game_data_transition_desc;

    /** @brief Authored UI text descriptor. */
    typedef struct sdl3d_game_data_ui_text
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
        sdl3d_game_data_ui_align align;
        /** @brief Runtime or authored scale multiplier applied during presentation. */
        float scale;
        /** @brief Whether alpha should pulse while visible. */
        bool pulse_alpha;
        /** @brief Text color. */
        sdl3d_color color;
    } sdl3d_game_data_ui_text;

    /** @brief Authored UI image descriptor. */
    typedef struct sdl3d_game_data_ui_image
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
        sdl3d_game_data_ui_align align;
        /** @brief Vertical alignment of the image rectangle around y. */
        sdl3d_game_data_ui_valign valign;
        /** @brief Runtime or authored scale multiplier applied around the image anchor. */
        float scale;
        /** @brief Image tint color. */
        sdl3d_color color;
    } sdl3d_game_data_ui_image;

    /** @brief Bit flags indicating which runtime UI state fields override authored descriptor values. */
    typedef enum sdl3d_game_data_ui_state_flags
    {
        /** @brief Override UI visibility. */
        SDL3D_GAME_DATA_UI_STATE_VISIBLE = 1u << 0,
        /** @brief Add a runtime x/y offset to the authored UI position. */
        SDL3D_GAME_DATA_UI_STATE_OFFSET = 1u << 1,
        /** @brief Multiply the authored UI scale. */
        SDL3D_GAME_DATA_UI_STATE_SCALE = 1u << 2,
        /** @brief Multiply the authored UI alpha. */
        SDL3D_GAME_DATA_UI_STATE_ALPHA = 1u << 3,
        /** @brief Multiply the authored UI tint/color. */
        SDL3D_GAME_DATA_UI_STATE_TINT = 1u << 4,
    } sdl3d_game_data_ui_state_flags;

    /**
     * @brief Runtime presentation state for an authored UI item.
     *
     * Runtime state is keyed by the UI item's authored `name`. It is layered on
     * top of static JSON descriptors during resolution, which lets timelines,
     * scripts, or host code animate UI elements without mutating game data.
     */
    typedef struct sdl3d_game_data_ui_state
    {
        /** @brief Combination of sdl3d_game_data_ui_state_flags values. */
        Uint32 flags;
        /** @brief Visibility override used when SDL3D_GAME_DATA_UI_STATE_VISIBLE is set. */
        bool visible;
        /** @brief Runtime x offset in the descriptor's coordinate space. */
        float offset_x;
        /** @brief Runtime y offset in the descriptor's coordinate space. */
        float offset_y;
        /** @brief Scale multiplier used when SDL3D_GAME_DATA_UI_STATE_SCALE is set. */
        float scale;
        /** @brief Alpha multiplier in [0, 1] used when SDL3D_GAME_DATA_UI_STATE_ALPHA is set. */
        float alpha;
        /** @brief Tint multiplier used when SDL3D_GAME_DATA_UI_STATE_TINT is set. */
        sdl3d_color tint;
    } sdl3d_game_data_ui_state;

    /**
     * @brief Callback for iterating authored UI text descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_ui_text_fn)(void *userdata, const sdl3d_game_data_ui_text *text);

    /**
     * @brief Callback for iterating authored UI image descriptors.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_ui_image_fn)(void *userdata, const sdl3d_game_data_ui_image *image);

    /** @brief Input mode used by a data-authored scene skip policy. */
    typedef enum sdl3d_game_data_skip_input
    {
        /** @brief The active scene cannot be skipped by input. */
        SDL3D_GAME_DATA_SKIP_INPUT_DISABLED = 0,
        /** @brief Any key, pointer, or gamepad press skips the scene. */
        SDL3D_GAME_DATA_SKIP_INPUT_ANY = 1,
        /** @brief A specific authored input action skips the scene. */
        SDL3D_GAME_DATA_SKIP_INPUT_ACTION = 2,
    } sdl3d_game_data_skip_input;

    /**
     * @brief Data-authored input policy for skipping the active scene.
     *
     * Skip policies are generic scene-flow primitives. They are appropriate
     * for splash screens, cutscenes, attract modes, and any scene whose author
     * wants controlled early advancement without hard-coding scene-specific
     * input handling.
     */
    typedef struct sdl3d_game_data_skip_policy
    {
        /** @brief True when this policy should be evaluated. */
        bool enabled;
        /** @brief Input source that can trigger the skip. */
        sdl3d_game_data_skip_input input;
        /** @brief Authored input action name when @p input is SDL3D_GAME_DATA_SKIP_INPUT_ACTION. */
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
    } sdl3d_game_data_skip_policy;

    /**
     * @brief Interaction policy for an active scene's autoplay timeline.
     *
     * These flags let intro, splash, attract, and cutscene authors decide
     * whether a still-running timeline owns scene flow or whether normal menus
     * and scene shortcuts remain interactive while timed events continue.
     */
    typedef struct sdl3d_game_data_timeline_policy
    {
        /** @brief True while an incomplete autoplay timeline suppresses active-scene menus. */
        bool block_menus;
        /** @brief True while an incomplete autoplay timeline suppresses authored scene shortcuts. */
        bool block_scene_shortcuts;
    } sdl3d_game_data_timeline_policy;

    /**
     * @brief Runtime state for a data-authored active-scene timeline.
     *
     * Hosts keep this state across frames. The game data runtime resets it
     * automatically when the active scene changes.
     */
    typedef struct sdl3d_game_data_timeline_state
    {
        /** @brief Runtime-owned active scene pointer currently tracked by this state. */
        const char *scene;
        /** @brief Elapsed timeline time in seconds for @p scene. */
        float time;
        /** @brief Next authored event index to evaluate. */
        int next_event_index;
        /** @brief True once all authored events have fired. */
        bool complete;
    } sdl3d_game_data_timeline_state;

    /**
     * @brief Result produced after advancing an active-scene timeline.
     */
    typedef struct sdl3d_game_data_timeline_update_result
    {
        /** @brief Scene requested by a `scene.request` timeline action, or NULL. */
        const char *scene_request;
        /** @brief Number of timeline actions executed during this update. */
        int actions_executed;
        /** @brief True when the active timeline has no more events to fire. */
        bool complete;
    } sdl3d_game_data_timeline_update_result;

    /**
     * @brief Runtime descriptor for the active scene's primary menu.
     *
     * Menus are authored in scene JSON files and map input actions to a
     * selected item. The runtime owns all string pointers.
     */
    typedef struct sdl3d_game_data_menu
    {
        /** @brief Stable menu name. */
        const char *name;
        /** @brief Input action that moves selection up, or -1. */
        int up_action_id;
        /** @brief Input action that moves selection down, or -1. */
        int down_action_id;
        /** @brief Input action that activates the selected item, or -1. */
        int select_action_id;
        /** @brief Signal emitted after successful navigation, or -1. */
        int move_signal_id;
        /** @brief Signal emitted when the selected item is activated, or -1. */
        int select_signal_id;
        /** @brief Currently selected zero-based item index. */
        int selected_index;
        /** @brief Number of selectable menu items. */
        int item_count;
    } sdl3d_game_data_menu;

    /** @brief Generic data-authored control kind for menu items. */
    typedef enum sdl3d_game_data_menu_control_type
    {
        /** @brief Menu item is a command, not a setting control. */
        SDL3D_GAME_DATA_MENU_CONTROL_NONE = 0,
        /** @brief Menu item toggles a boolean actor property. */
        SDL3D_GAME_DATA_MENU_CONTROL_TOGGLE = 1,
        /** @brief Menu item cycles through authored choices. */
        SDL3D_GAME_DATA_MENU_CONTROL_CHOICE = 2,
        /** @brief Menu item increments a numeric property within a range. */
        SDL3D_GAME_DATA_MENU_CONTROL_RANGE = 3,
    } sdl3d_game_data_menu_control_type;

    /** @brief App pause command authored on a menu item. */
    typedef enum sdl3d_game_data_menu_pause_command
    {
        /** @brief Selecting the item does not change pause state. */
        SDL3D_GAME_DATA_MENU_PAUSE_NONE = 0,
        /** @brief Selecting the item pauses the app. */
        SDL3D_GAME_DATA_MENU_PAUSE_PAUSE = 1,
        /** @brief Selecting the item resumes the app. */
        SDL3D_GAME_DATA_MENU_PAUSE_RESUME = 2,
        /** @brief Selecting the item toggles the app pause state. */
        SDL3D_GAME_DATA_MENU_PAUSE_TOGGLE = 3,
    } sdl3d_game_data_menu_pause_command;

    /**
     * @brief Runtime descriptor for one authored menu item.
     *
     * A menu item may request a scene change, request app quit, emit a signal,
     * change pause state, return to a previously authored scene, or mutate an
     * actor property as a generic option control. Hosts can use these fields
     * directly or translate them into a higher-level scene transition flow.
     */
    typedef struct sdl3d_game_data_menu_item
    {
        /** @brief Display label for the item. */
        const char *label;
        /** @brief Target scene name, or NULL when this item does not change scene. */
        const char *scene;
        /** @brief Scene stored as the return target when this item changes scene, or NULL. */
        const char *return_to;
        /** @brief True when selecting this item requests the stored return scene. */
        bool return_scene;
        /** @brief True when selecting this item requests application quit. */
        bool quit;
        /** @brief Signal emitted by this item, or -1 when not authored. */
        int signal_id;
        /** @brief Authored pause command to apply when selecting this item. */
        sdl3d_game_data_menu_pause_command pause_command;
        /** @brief True when selecting this item stores a return pause state. */
        bool has_return_paused;
        /** @brief Pause state stored for a later return_scene item. */
        bool return_paused;
        /** @brief Authored generic control type. */
        sdl3d_game_data_menu_control_type control_type;
        /** @brief Actor that owns the controlled property, or NULL. */
        const char *control_target;
        /** @brief Controlled property key, or NULL. */
        const char *control_key;
        /** @brief Number of authored choices for choice controls. */
        int choice_count;
    } sdl3d_game_data_menu_item;

    /** @brief Authored scene transition behavior policy. */
    typedef struct sdl3d_game_data_scene_transition_policy
    {
        /** @brief Permit requesting the currently active scene. */
        bool allow_same_scene;
        /** @brief Permit a new scene request to replace an active transition. */
        bool allow_interrupt;
        /** @brief Reset menu input arming after an accepted scene request. */
        bool reset_menu_input_on_request;
    } sdl3d_game_data_scene_transition_policy;

    /** @brief Authored input shortcut that requests a scene change. */
    typedef struct sdl3d_game_data_scene_shortcut
    {
        /** @brief Input action id resolved from the authored action name, or -1. */
        int action_id;
        /** @brief Authored input action name. */
        const char *action;
        /** @brief Target scene name. */
        const char *scene;
    } sdl3d_game_data_scene_shortcut;

    /** @brief Read-only descriptor for an authored particle emitter component. */
    typedef struct sdl3d_game_data_particle_emitter
    {
        /** @brief Name of the entity that owns the emitter. */
        const char *entity_name;
        /** @brief Emitter configuration evaluated from authored data and actor position. */
        sdl3d_particle_config config;
        /** @brief Draw-time emissive color to apply around particle rendering. */
        sdl3d_vec3 draw_emissive;
    } sdl3d_game_data_particle_emitter;

    /**
     * @brief Callback for iterating active authored particle emitters.
     *
     * Return false to stop iteration early.
     */
    typedef bool (*sdl3d_game_data_particle_emitter_fn)(void *userdata,
                                                        const sdl3d_game_data_particle_emitter *emitter);

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
     * sdl3d_properties setters.
     */
    sdl3d_properties *sdl3d_game_data_mutable_scene_state(sdl3d_game_data_runtime *runtime);

    /**
     * @brief Read the persistent scene-state bag.
     *
     * @see sdl3d_game_data_mutable_scene_state
     */
    const sdl3d_properties *sdl3d_game_data_scene_state(const sdl3d_game_data_runtime *runtime);

    /** @brief Authored game data diagnostic severity. */
    typedef enum sdl3d_game_data_diagnostic_severity
    {
        /** @brief Non-fatal issue that authors should review. */
        SDL3D_GAME_DATA_DIAGNOSTIC_WARNING = 1,
        /** @brief Fatal issue that prevents the data from loading. */
        SDL3D_GAME_DATA_DIAGNOSTIC_ERROR = 2,
    } sdl3d_game_data_diagnostic_severity;

    /**
     * @brief Callback for authored game data validation diagnostics.
     *
     * @p json_path is a best-effort JSON path to the authored object or field
     * that produced the diagnostic. @p message is a human-readable description
     * intended to be actionable without stepping through engine code.
     */
    typedef void (*sdl3d_game_data_diagnostic_fn)(void *userdata, sdl3d_game_data_diagnostic_severity severity,
                                                  const char *json_path, const char *message);

    /**
     * @brief Options controlling authored game data validation.
     */
    typedef struct sdl3d_game_data_validation_options
    {
        /** @brief Optional diagnostic callback. */
        sdl3d_game_data_diagnostic_fn diagnostic;
        /** @brief User pointer passed to @p diagnostic. */
        void *userdata;
        /** @brief When true, warnings also make validation fail. */
        bool treat_warnings_as_errors;
    } sdl3d_game_data_validation_options;

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
    typedef bool (*sdl3d_game_data_adapter_fn)(void *userdata, sdl3d_game_data_runtime *runtime,
                                               const char *adapter_name, sdl3d_registered_actor *target,
                                               const sdl3d_properties *payload);

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
    bool sdl3d_game_data_load_file(const char *path, sdl3d_game_session *session, sdl3d_game_data_runtime **out_runtime,
                                   char *error_buffer, int error_buffer_size);

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
    bool sdl3d_game_data_load_asset(sdl3d_asset_resolver *assets, const char *asset_path, sdl3d_game_session *session,
                                    sdl3d_game_data_runtime **out_runtime, char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data asset.
     *
     * This lightweight reader is intended for startup, before a managed loop
     * creates a window or game session. Missing fields keep the values already
     * present in @p out_config, so callers can initialize defaults first. When
     * an authored title is present, it is copied into @p title_buffer and
     * out_config->title points at that buffer.
     */
    bool sdl3d_game_data_load_app_config_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                               sdl3d_game_config *out_config, char *title_buffer, int title_buffer_size,
                                               char *error_buffer, int error_buffer_size);

    /**
     * @brief Read the managed-loop config authored in a JSON game data file.
     *
     * Missing fields keep the values already present in @p out_config, so
     * callers can initialize defaults first. When an authored title is present,
     * it is copied into @p title_buffer and out_config->title points at that
     * buffer.
     */
    bool sdl3d_game_data_load_app_config_file(const char *path, sdl3d_game_config *out_config, char *title_buffer,
                                              int title_buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Get the writable storage identity authored by the game data.
     *
     * The returned config is suitable for sdl3d_storage_create(). String
     * pointers are owned by @p runtime and remain valid until
     * sdl3d_game_data_destroy(). If the JSON omits the storage block, SDL3D
     * derives conservative defaults from metadata/app fields and finally falls
     * back to sdl3d_storage_config_init() defaults.
     *
     * @param runtime Loaded game data runtime.
     * @param out_config Receives the resolved storage configuration.
     * @return true when @p out_config was filled.
     */
    bool sdl3d_game_data_get_storage_config(const sdl3d_game_data_runtime *runtime, sdl3d_storage_config *out_config);

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
    bool sdl3d_game_data_validate_file(const char *path, const sdl3d_game_data_validation_options *options,
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
    bool sdl3d_game_data_validate_asset(sdl3d_asset_resolver *assets, const char *asset_path,
                                        const sdl3d_game_data_validation_options *options, char *error_buffer,
                                        int error_buffer_size);

    /**
     * @brief Destroy a loaded game data runtime.
     *
     * Disconnects installed signal handlers and frees the parsed document.
     * Session services and registered actors are not destroyed.
     */
    void sdl3d_game_data_destroy(sdl3d_game_data_runtime *runtime);

    /**
     * @brief Register a named native game-specific adapter callback.
     *
     * Re-registering a name replaces the callback and userdata. If the JSON file
     * declared a Lua function for the same adapter, the native callback becomes
     * the active implementation. The adapter name is copied by the runtime.
     */
    bool sdl3d_game_data_register_adapter(sdl3d_game_data_runtime *runtime, const char *name,
                                          sdl3d_game_data_adapter_fn callback, void *userdata);

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
     * Native adapters registered with sdl3d_game_data_register_adapter() remain
     * active and are not replaced by reloaded Lua functions.
     *
     * @param runtime Loaded game data runtime.
     * @param assets Resolver containing the updated script assets.
     * @param error_buffer Optional buffer for a human-readable error.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when scripts were reloaded and committed, or when the runtime
     * has no scripts to reload.
     */
    bool sdl3d_game_data_reload_scripts(sdl3d_game_data_runtime *runtime, sdl3d_asset_resolver *assets,
                                        char *error_buffer, int error_buffer_size);

    /**
     * @brief Advance JSON-authored controllers, motion, and sensors by one tick.
     *
     * Call after input is refreshed and before rendering. This updates generic
     * control/motion components, invokes controller adapters, evaluates sensors,
     * and emits any authored signals.
     */
    bool sdl3d_game_data_update(sdl3d_game_data_runtime *runtime, float dt);

    /** @brief Find an authored signal id by name, or -1 when missing. */
    int sdl3d_game_data_find_signal(const sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored input action id by name, or -1 when missing. */
    int sdl3d_game_data_find_action(const sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Find an authored actor by name in the runtime's session registry. */
    sdl3d_registered_actor *sdl3d_game_data_find_actor(const sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Find the first authored actor whose entity data contains @p tag. */
    sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tag(const sdl3d_game_data_runtime *runtime,
                                                                const char *tag);

    /**
     * @brief Find the first authored actor whose entity data contains every tag.
     *
     * Tags are matched against the entity's `tags` array in the loaded JSON
     * document. This lets game code request roles like `{"paddle", "player"}`
     * without depending on exact entity names.
     */
    sdl3d_registered_actor *sdl3d_game_data_find_actor_with_tags(const sdl3d_game_data_runtime *runtime,
                                                                 const char *const *tags, int tag_count);

    /**
     * @brief Read data-authored application lifecycle hooks.
     *
     * Missing fields return neutral values: signal/action ids are -1 and
     * transition names are NULL.
     */
    bool sdl3d_game_data_get_app_control(const sdl3d_game_data_runtime *runtime,
                                         sdl3d_game_data_app_control *out_control);

    /**
     * @brief Return whether an authored signal should apply live window settings.
     *
     * Games declare these signals under `app.window.apply_signal` or
     * `app.window.apply_signals`. This lets reusable menu controls apply display
     * mode, renderer, and V-sync changes immediately without hard-coding menu
     * names in the host.
     */
    bool sdl3d_game_data_app_signal_applies_window_settings(const sdl3d_game_data_runtime *runtime, int signal_id);

    /**
     * @brief Evaluate the data-authored app pause condition.
     *
     * Returns true when `app.pause.allowed_if` is absent. When present, the
     * condition uses the same generic condition language as UI visibility:
     * actor property comparisons, app pause checks, camera checks, and
     * all/any/not composition. @p metrics may be NULL when the condition does
     * not refer to app metrics.
     */
    bool sdl3d_game_data_app_pause_allowed(const sdl3d_game_data_runtime *runtime,
                                           const sdl3d_game_data_ui_metrics *metrics);

    /**
     * @brief Advance data-authored presentation clocks.
     *
     * Presentation clocks are generic data-driven oscillators/counters used by
     * UI, lights, and other render-facing effects. Authored clocks may write
     * into actor properties so scripts, UI bindings, and render evaluation can
     * share the same source of truth.
     */
    bool sdl3d_game_data_update_presentation_clocks(sdl3d_game_data_runtime *runtime, float dt, bool paused,
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
    bool sdl3d_game_data_update_scene_activity(sdl3d_game_data_runtime *runtime, const sdl3d_input_manager *input,
                                               float dt);

    /**
     * @brief Read the authored UI pulse phase.
     *
     * Returns @p fallback when no `presentation.ui_pulse_clock` is authored or
     * the named clock has no current value.
     */
    float sdl3d_game_data_ui_pulse_phase(const sdl3d_game_data_runtime *runtime, float fallback);

    /**
     * @brief Read authored FPS metric sample duration in seconds.
     */
    float sdl3d_game_data_fps_sample_seconds(const sdl3d_game_data_runtime *runtime, float fallback);

    /** @brief Read a font asset descriptor by id from `assets.fonts`. */
    bool sdl3d_game_data_get_font_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                        sdl3d_game_data_font_asset *out_font);

    /** @brief Read an image asset descriptor by id from `assets.images`. */
    bool sdl3d_game_data_get_image_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                         sdl3d_game_data_image_asset *out_image);

    /** @brief Read a sound-effect asset descriptor by id from `assets.sounds`. */
    bool sdl3d_game_data_get_sound_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                         sdl3d_game_data_sound_asset *out_sound);

    /** @brief Read a music asset descriptor by id from `assets.music`. */
    bool sdl3d_game_data_get_music_asset(const sdl3d_game_data_runtime *runtime, const char *id,
                                         sdl3d_game_data_music_asset *out_music);

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
    bool sdl3d_game_data_prepare_audio_file(sdl3d_game_data_runtime *runtime, const char *path, char *out_path,
                                            int out_path_size);

    /**
     * @brief Return the currently active authored camera name.
     *
     * The returned pointer is owned by the parsed JSON document and remains
     * valid until the runtime is destroyed.
     */
    const char *sdl3d_game_data_active_camera(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored non-adapter camera by name.
     *
     * Adapter cameras are game-specific and return false here because their
     * final pose is computed by game code or script. Orthographic cameras use
     * `size` as sdl3d_camera3d::fovy; perspective cameras use `fovy`.
     */
    bool sdl3d_game_data_get_camera(const sdl3d_game_data_runtime *runtime, const char *name,
                                    sdl3d_camera3d *out_camera);

    /**
     * @brief Read a numeric custom property from an authored camera.
     *
     * This lets games keep camera tuning data in JSON even when the camera pose
     * itself is adapter-driven.
     */
    bool sdl3d_game_data_get_camera_float(const sdl3d_game_data_runtime *runtime, const char *camera_name,
                                          const char *property_name, float *out_value);

    /** @brief Return the number of authored world lights. */
    int sdl3d_game_data_world_light_count(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Read the authored world ambient light color.
     *
     * Values are linear RGB in the same range expected by
     * sdl3d_set_ambient_light().
     */
    bool sdl3d_game_data_get_world_ambient_light(const sdl3d_game_data_runtime *runtime, float out_rgb[3]);

    /**
     * @brief Read an authored world light by zero-based index.
     *
     * The returned light is suitable for passing to sdl3d_add_light().
     */
    bool sdl3d_game_data_get_world_light(const sdl3d_game_data_runtime *runtime, int index, sdl3d_light *out_light);

    /**
     * @brief Read an authored world light with generic visual effects evaluated.
     *
     * Supported light effects include `pulse` and `flash`, allowing data to
     * drive color blends, intensity changes, and range changes over time or
     * from actor properties. Passing NULL for @p eval uses a zeroed evaluation
     * context.
     */
    bool sdl3d_game_data_get_world_light_evaluated(const sdl3d_game_data_runtime *runtime, int index,
                                                   const sdl3d_game_data_render_eval *eval, sdl3d_light *out_light);

    /**
     * @brief Iterate active authored render primitive components.
     *
     * Components currently supported by this iterator are `render.cube` and
     * `render.sphere`. Iteration skips inactive actors.
     */
    bool sdl3d_game_data_for_each_render_primitive(const sdl3d_game_data_runtime *runtime,
                                                   sdl3d_game_data_render_primitive_fn callback, void *userdata);

    /**
     * @brief Iterate active authored render primitives with dynamic effects evaluated.
     *
     * This applies generic `effects` authored on render primitive components,
     * such as property-driven flash colors, size offsets, and time-driven
     * pulses. Passing NULL for @p eval uses a zeroed evaluation context.
     */
    bool sdl3d_game_data_for_each_render_primitive_evaluated(const sdl3d_game_data_runtime *runtime,
                                                             const sdl3d_game_data_render_eval *eval,
                                                             sdl3d_game_data_render_primitive_fn callback,
                                                             void *userdata);

    /**
     * @brief Read an authored particle emitter component from an entity.
     *
     * The returned config is ready for sdl3d_create_particle_emitter(). Texture
     * references are intentionally not resolved here yet, so config.texture is
     * always NULL.
     */
    bool sdl3d_game_data_get_particle_emitter(const sdl3d_game_data_runtime *runtime, const char *entity_name,
                                              sdl3d_particle_config *out_config);

    /**
     * @brief Read optional draw-time emissive color for a particle emitter entity.
     *
     * The color is read from the emitter component's `draw_emissive` field and
     * defaults to zero when not authored.
     */
    bool sdl3d_game_data_get_particle_emitter_draw_emissive(const sdl3d_game_data_runtime *runtime,
                                                            const char *entity_name, sdl3d_vec3 *out_rgb);

    /**
     * @brief Iterate active authored particle emitter components.
     *
     * Iteration skips inactive actors and entities not included by the active
     * scene. The descriptor's config is ready for sdl3d_create_particle_emitter().
     */
    bool sdl3d_game_data_for_each_particle_emitter(const sdl3d_game_data_runtime *runtime,
                                                   sdl3d_game_data_particle_emitter_fn callback, void *userdata);

    /**
     * @brief Read authored render setup.
     *
     * Missing fields produce conservative defaults: black clear color, lighting
     * enabled, bloom/SSAO enabled, and ACES tonemapping.
     */
    bool sdl3d_game_data_get_render_settings(const sdl3d_game_data_runtime *runtime,
                                             sdl3d_game_data_render_settings *out_settings);

    /**
     * @brief Read a named authored transition descriptor.
     *
     * @p name is looked up under the top-level `transitions` object.
     */
    bool sdl3d_game_data_get_transition(const sdl3d_game_data_runtime *runtime, const char *name,
                                        sdl3d_game_data_transition_desc *out_transition);

    /**
     * @brief Return the active scene name.
     *
     * Scene names come from the top-level `scenes.initial` field and referenced
     * scene files. Returns NULL when the game does not author scenes.
     */
    const char *sdl3d_game_data_active_scene(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Return the number of authored scenes loaded by the runtime.
     *
     * Games without a `scenes.files` manifest return 0. Scene order matches
     * the authored manifest order and is stable for the lifetime of the
     * runtime.
     */
    int sdl3d_game_data_scene_count(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Return an authored scene name by manifest index.
     *
     * @param index Zero-based scene index in the loaded manifest.
     * @return Runtime-owned scene name, or NULL when @p index is out of range.
     */
    const char *sdl3d_game_data_scene_name_at(const sdl3d_game_data_runtime *runtime, int index);

    /**
     * @brief Switch to an authored scene by name.
     *
     * The new scene's `on_enter_signal`, when present, is emitted after the
     * active scene changes. The enter payload always includes `from_scene` and
     * `to_scene` string keys. Returns false when @p scene_name is unknown.
     */
    bool sdl3d_game_data_set_active_scene(sdl3d_game_data_runtime *runtime, const char *scene_name);

    /**
     * @brief Switch to an authored scene and pass state to its enter signal.
     *
     * @p payload is copied into a transient enter payload and forwarded to the
     * target scene's `on_enter_signal`; the caller keeps ownership of @p
     * payload. The runtime also writes `from_scene` and `to_scene`, overriding
     * same-named keys in @p payload so every scene-enter observer receives
     * reliable transition context.
     *
     * Use sdl3d_game_data_mutable_scene_state() for data that must persist
     * after enter-signal processing.
     */
    bool sdl3d_game_data_set_active_scene_with_payload(sdl3d_game_data_runtime *runtime, const char *scene_name,
                                                       const sdl3d_properties *payload);

    /**
     * @brief Return whether the active scene should advance gameplay systems.
     *
     * Scenes default to updating gameplay when they do not specify
     * `updates_game`. Games without authored scenes return true.
     */
    bool sdl3d_game_data_active_scene_updates_game(const sdl3d_game_data_runtime *runtime);

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
    bool sdl3d_game_data_active_scene_update_phase(const sdl3d_game_data_runtime *runtime, const char *phase,
                                                   bool paused);

    /**
     * @brief Return whether the active scene should render the authored world.
     *
     * Scenes default to rendering the world when they do not specify
     * `renders_world`. Games without authored scenes return true.
     */
    bool sdl3d_game_data_active_scene_renders_world(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Return whether an entity belongs to the active scene.
     *
     * Scenes that omit an `entities` list include all loaded entities for
     * backward compatibility. Scenes with an empty list include no entities.
     */
    bool sdl3d_game_data_active_scene_has_entity(const sdl3d_game_data_runtime *runtime, const char *entity_name);

    /**
     * @brief Return whether the active scene allows an input action.
     *
     * If a scene omits `input.actions`, all actions are allowed. When present,
     * the action must be listed there or in top-level
     * `app.input_policy.global_actions`.
     */
    bool sdl3d_game_data_active_scene_allows_action(const sdl3d_game_data_runtime *runtime, int action_id);

    /**
     * @brief Read authored scene transition policy.
     *
     * Missing fields use stable defaults: same-scene requests and interrupting
     * active transitions are rejected, and accepted scene requests reset menu
     * input arming.
     */
    bool sdl3d_game_data_get_scene_transition_policy(const sdl3d_game_data_runtime *runtime,
                                                     sdl3d_game_data_scene_transition_policy *out_policy);

    /**
     * @brief Read the transition descriptor attached to a scene phase.
     *
     * @p phase is commonly `enter` or `exit` and is looked up under the scene's
     * `transitions` object. Returns false when the scene or phase is missing.
     */
    bool sdl3d_game_data_get_scene_transition(const sdl3d_game_data_runtime *runtime, const char *scene_name,
                                              const char *phase, sdl3d_game_data_transition_desc *out_transition);

    /**
     * @brief Read the active scene's primary menu, if any.
     *
     * The first menu whose optional `active_if` condition passes is considered
     * active. Conditions that depend on frame metrics, such as `app.paused`,
     * evaluate as false through this convenience wrapper.
     */
    bool sdl3d_game_data_get_active_menu(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_menu *out_menu);

    /**
     * @brief Read the active scene menu using current frame metrics.
     *
     * This variant lets authored menu `active_if` conditions depend on app
     * pause state, camera state, actor properties, or other metrics-backed UI
     * conditions. It returns false when no active scene menu is eligible.
     */
    bool sdl3d_game_data_get_active_menu_for_metrics(const sdl3d_game_data_runtime *runtime,
                                                     const sdl3d_game_data_ui_metrics *metrics,
                                                     sdl3d_game_data_menu *out_menu);

    /**
     * @brief Move a menu selection by @p delta with wrap-around.
     *
     * Positive values move down, negative values move up. Returns false when
     * the menu is unknown or contains no items.
     */
    bool sdl3d_game_data_menu_move(sdl3d_game_data_runtime *runtime, const char *menu_name, int delta);

    /**
     * @brief Read one item from an authored menu.
     *
     * @p index is zero based. The returned strings remain owned by the runtime.
     */
    bool sdl3d_game_data_get_menu_item(const sdl3d_game_data_runtime *runtime, const char *menu_name, int index,
                                       sdl3d_game_data_menu_item *out_item);

    /**
     * @brief Apply the generic control behavior authored on a menu item.
     *
     * Toggle controls flip boolean properties, choice controls advance to the
     * next authored choice, and range controls increment numeric properties with
     * wrapping. Returns false when @p item is not a control or its target cannot
     * be resolved.
     */
    bool sdl3d_game_data_apply_menu_item_control(sdl3d_game_data_runtime *runtime,
                                                 const sdl3d_game_data_menu_item *item);

    /**
     * @brief Return the number of scene shortcuts authored under `app.scene_shortcuts`.
     */
    int sdl3d_game_data_scene_shortcut_count(const sdl3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored scene shortcut by index.
     *
     * Invalid or unresolved shortcut entries still return their authored names,
     * but use action id -1. This lets validators and hosts report useful
     * diagnostics without failing at runtime.
     */
    bool sdl3d_game_data_scene_shortcut_at(const sdl3d_game_data_runtime *runtime, int index,
                                           sdl3d_game_data_scene_shortcut *out_shortcut);

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
    bool sdl3d_game_data_active_menu_input_is_idle(const sdl3d_game_data_runtime *runtime,
                                                   const sdl3d_input_manager *input);

    /** @brief Iterate authored UI text descriptors. */
    bool sdl3d_game_data_for_each_ui_text(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_text_fn callback,
                                          void *userdata);

    /**
     * @brief Iterate authored UI images visible to the active scene.
     *
     * Iteration includes global `ui.images` followed by active-scene
     * `ui.images`. Visibility is evaluated separately by
     * sdl3d_game_data_ui_image_is_visible().
     */
    bool sdl3d_game_data_for_each_ui_image(const sdl3d_game_data_runtime *runtime, sdl3d_game_data_ui_image_fn callback,
                                           void *userdata);

    /**
     * @brief Initialize runtime UI state to identity values.
     *
     * The initialized state has no override flags, zero offset, scale 1, alpha
     * 1, and white tint.
     */
    void sdl3d_game_data_ui_state_init(sdl3d_game_data_ui_state *state);

    /**
     * @brief Store runtime state for a named authored UI item.
     *
     * The runtime copies @p state and owns the name key internally. State
     * remains active until replaced, cleared by name, or all UI state is
     * cleared.
     */
    bool sdl3d_game_data_set_ui_state(sdl3d_game_data_runtime *runtime, const char *name,
                                      const sdl3d_game_data_ui_state *state);

    /**
     * @brief Read runtime state for a named authored UI item.
     *
     * Returns false when no state exists for @p name. @p out_state is
     * initialized to identity values before lookup.
     */
    bool sdl3d_game_data_get_ui_state(const sdl3d_game_data_runtime *runtime, const char *name,
                                      sdl3d_game_data_ui_state *out_state);

    /** @brief Clear runtime state for one named UI item. */
    bool sdl3d_game_data_clear_ui_state(sdl3d_game_data_runtime *runtime, const char *name);

    /** @brief Clear all runtime UI item state. */
    void sdl3d_game_data_clear_ui_states(sdl3d_game_data_runtime *runtime);

    /**
     * @brief Resolve authored text plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_text may alias @p text.
     */
    bool sdl3d_game_data_resolve_ui_text(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                         const sdl3d_game_data_ui_metrics *metrics, sdl3d_game_data_ui_text *out_text,
                                         bool *out_visible);

    /**
     * @brief Resolve authored image plus runtime UI state for presentation.
     *
     * @p out_visible receives the final visibility after authored conditions
     * and runtime overrides. @p out_image may alias @p image.
     */
    bool sdl3d_game_data_resolve_ui_image(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_image *image,
                                          const sdl3d_game_data_ui_metrics *metrics,
                                          sdl3d_game_data_ui_image *out_image, bool *out_visible);

    /**
     * @brief Evaluate a UI text descriptor's authored visibility condition.
     *
     * Supports camera-active checks, app pause checks, actor property
     * comparisons, and boolean all/any/not composition. Descriptors without a
     * condition are visible.
     */
    bool sdl3d_game_data_ui_text_is_visible(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                            const sdl3d_game_data_ui_metrics *metrics);

    /** @brief Evaluate a UI image's authored `visible_if` condition. */
    bool sdl3d_game_data_ui_image_is_visible(const sdl3d_game_data_runtime *runtime,
                                             const sdl3d_game_data_ui_image *image,
                                             const sdl3d_game_data_ui_metrics *metrics);

    /**
     * @brief Read the active scene's authored skip policy.
     *
     * The runtime first checks `scene.skip_policy`, then
     * `scene.timeline.skip_policy`. Returns false when no enabled policy is
     * authored for the active scene. Missing fields use conservative defaults:
     * any input, transition preservation enabled, and input consumption enabled.
     */
    bool sdl3d_game_data_get_active_skip_policy(const sdl3d_game_data_runtime *runtime,
                                                sdl3d_game_data_skip_policy *out_policy);

    /**
     * @brief Read the active scene's authored timeline interaction policy.
     *
     * Returns false when the active scene has no autoplaying `timeline` object.
     * Missing policy fields default to false so timelines remain interactive
     * unless the scene author explicitly blocks menus or scene shortcuts.
     */
    bool sdl3d_game_data_get_active_timeline_policy(const sdl3d_game_data_runtime *runtime,
                                                    sdl3d_game_data_timeline_policy *out_policy);

    /**
     * @brief Initialize reusable timeline state.
     *
     * Safe to call with NULL.
     */
    void sdl3d_game_data_timeline_state_init(sdl3d_game_data_timeline_state *state);

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
    bool sdl3d_game_data_update_timeline(sdl3d_game_data_runtime *runtime, sdl3d_game_data_timeline_state *state,
                                         float dt, sdl3d_game_data_timeline_update_result *out_result);

    /**
     * @brief Advance data-authored runtime animations.
     *
     * Animations are started by generic actions such as `ui.animate` and
     * `property.animate`. This function advances active tweens, applies eased
     * values to UI runtime state or actor properties, emits completion signals
     * for one-shot animations, and clears scene-scoped animation state when the
     * active scene changes.
     *
     * @param runtime Runtime created by sdl3d_game_data_load_file().
     * @param dt Delta time in seconds.
     * @return true when active animations were advanced successfully.
     */
    bool sdl3d_game_data_update_animations(sdl3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Resolve UI text content from data-authored bindings.
     *
     * Literal `text` entries are copied directly. Entries with `bindings`
     * resolve engine metrics and actor properties, then format them using the
     * descriptor's `format` string.
     */
    bool sdl3d_game_data_format_ui_text(const sdl3d_game_data_runtime *runtime, const sdl3d_game_data_ui_text *text,
                                        const sdl3d_game_data_ui_metrics *metrics, char *buffer, size_t buffer_size);

    /**
     * @brief Advance data-authored property animation components.
     *
     * Currently supports `property.decay` components, which move a numeric
     * actor property toward a target value at an authored rate. This is useful
     * for reusable presentation state such as flashes, glow weights, and other
     * transient values without per-game C code.
     */
    bool sdl3d_game_data_update_property_effects(sdl3d_game_data_runtime *runtime, float dt);

    /**
     * @brief Return the dt currently being processed by sdl3d_game_data_update().
     *
     * Adapter callbacks can use this for per-frame controller behavior. Outside
     * an update call, this returns the most recent non-negative update dt.
     */
    float sdl3d_game_data_delta_time(const sdl3d_game_data_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_GAME_DATA_H */

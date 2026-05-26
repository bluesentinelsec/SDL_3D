/**
 * @file game_data_scene.h
 * @brief Scene flow, menu, transition, and particle-emitter descriptors for JSON-authored games.
 */

#ifndef SLAYER3D_GAME_DATA_SCENE_H
#define SLAYER3D_GAME_DATA_SCENE_H

#include <stdbool.h>

#include "slayer3d/effects.h"
#include "slayer3d/game_data_defaults.h"
#include "slayer3d/transition.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

    /** @brief Result produced after advancing an active-scene timeline. */
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

#ifdef __cplusplus
}
#endif

#endif

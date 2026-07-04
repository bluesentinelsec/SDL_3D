/**
 * @file game_data_editor_lock.c
 * @brief Editor-only lock toggles and mutation guards for brushes and Things.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

bool slayer3d_game_data_editor_actor_locked(const slayer3d_game_data_runtime *runtime, const char *name)
{
    const editor_actor_runtime *actor = editor_runtime_find_actor(runtime, name);
    return actor != NULL && actor->locked;
}

bool slayer3d_game_data_editor_selection_contains_locked_objects(const slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_runtime *mutable_runtime = (slayer3d_game_data_runtime *)runtime;
    if (runtime == NULL)
        return false;

    if (editor_selected_brushes_active_for_scene(runtime))
    {
        for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
        {
            const editor_brush_source_box_runtime *box = editor_runtime_find_source_box_for_selection(
                mutable_runtime, &runtime->editor_selected_brushes[i], NULL);
            if (box != NULL && box->locked)
                return true;
        }
    }

    const editor_actor_runtime *actor = editor_runtime_active_selected_actor(runtime);
    return actor != NULL && actor->locked;
}

void publish_editor_lock_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int locked_count = editor_runtime_count_objects_with_state(runtime, EDITOR_OBJECT_STATE_LOCKED);
    slayer3d_properties_set_int(runtime->scene_state, "editor.lock.locked_count", locked_count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.lock.has_locked", locked_count > 0);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.lock.selection_has_locked",
                                 slayer3d_game_data_editor_selection_contains_locked_objects(runtime));
}

bool slayer3d_game_data_reject_locked_editor_selection_action(slayer3d_game_data_runtime *runtime, const char *message)
{
    const char *text = message != NULL && message[0] != '\0' ? message : "selection contains locked objects";
    if (runtime != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", text);
    editor_publish_console_message(runtime, text);
    publish_editor_lock_state(runtime);
    return true;
}

static bool set_selected_actor_lock(slayer3d_game_data_runtime *runtime, bool locked)
{
    editor_actor_runtime *actor = editor_runtime_active_selected_actor(runtime);
    if (actor == NULL || actor->locked == locked)
        return false;
    actor->locked = locked;
    return true;
}

static int set_selected_brush_locks(slayer3d_game_data_runtime *runtime, bool locked)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime))
        return 0;

    int changed = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        editor_brush_source_box_runtime *box =
            editor_runtime_find_source_box_for_selection(runtime, &runtime->editor_selected_brushes[i], NULL);
        if (box == NULL || box->locked == locked)
            continue;
        box->locked = locked;
        ++changed;
    }
    return changed;
}

static bool set_selected_lock_action(slayer3d_game_data_runtime *runtime, bool locked)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const int changed_brushes = set_selected_brush_locks(runtime, locked);
    const bool changed_actor = set_selected_actor_lock(runtime, locked);
    const int changed = changed_brushes + (changed_actor ? 1 : 0);
    char message[96];
    if (changed > 0)
    {
        SDL_snprintf(message, sizeof(message), "%s %d selected object%s", locked ? "locked" : "unlocked", changed,
                     changed == 1 ? "" : "s");
    }
    else
    {
        SDL_snprintf(message, sizeof(message), "nothing selected to %s", locked ? "lock" : "unlock");
    }
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    publish_editor_lock_state(runtime);
    return true;
}

bool slayer3d_game_data_lock_selected_editor_objects_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    return set_selected_lock_action(runtime, true);
}

bool slayer3d_game_data_unlock_selected_editor_objects_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    return set_selected_lock_action(runtime, false);
}

bool slayer3d_game_data_unlock_all_editor_objects_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    int unlocked_count = 0;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int i = 0; i < world->editor_source_box_count; ++i)
        {
            if (!world->editor_source_boxes[i].locked)
                continue;
            world->editor_source_boxes[i].locked = false;
            ++unlocked_count;
        }
    }
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (!runtime->editor_actors[i].locked)
            continue;
        runtime->editor_actors[i].locked = false;
        ++unlocked_count;
    }

    char message[96];
    if (unlocked_count > 0)
        SDL_snprintf(message, sizeof(message), "unlocked %d object%s", unlocked_count, unlocked_count == 1 ? "" : "s");
    else
        SDL_strlcpy(message, "no locked objects", sizeof(message));
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    publish_editor_lock_state(runtime);
    publish_editor_selected_brush_count(runtime);
    return true;
}

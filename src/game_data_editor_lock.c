/**
 * @file game_data_editor_lock.c
 * @brief Editor-only lock toggles and mutation guards for brushes and Things.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static int count_locked_editor_objects(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return 0;

    int count = 0;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int i = 0; i < world->editor_source_box_count; ++i)
        {
            if (world->editor_source_boxes[i].locked)
                ++count;
        }
    }
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (runtime->editor_actors[i].locked)
            ++count;
    }
    return count;
}

static const char *selection_brush_identity(const slayer3d_game_data_editor_selection *selection)
{
    if (selection == NULL)
        return NULL;
    if (selection->element_editor != NULL && selection->element_editor->stable_id != NULL &&
        selection->element_editor->stable_id[0] != '\0')
    {
        return selection->element_editor->stable_id;
    }
    return selection->element_name;
}

static editor_brush_source_box_runtime *source_box_for_selection(slayer3d_game_data_runtime *runtime,
                                                                 const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || selection == NULL || !selection->hit ||
        selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD || selection->world_name == NULL)
    {
        return NULL;
    }

    brush_world_runtime *world = find_brush_world_runtime_mutable(runtime, selection->world_name);
    if (world == NULL || !world->editor_has_source_model)
        return NULL;

    int source_index = editor_brush_world_find_source_box_index(world, selection_brush_identity(selection));
    if (source_index < 0 && selection->element_name != NULL)
        source_index = editor_brush_world_find_source_box_index(world, selection->element_name);
    if (source_index < 0 || source_index >= world->editor_source_box_count)
        return NULL;
    return &world->editor_source_boxes[source_index];
}

static editor_actor_runtime *editor_actor_for_name(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (runtime->editor_actors[i].name != NULL && SDL_strcmp(runtime->editor_actors[i].name, name) == 0)
            return &runtime->editor_actors[i];
    }
    return NULL;
}

static editor_actor_runtime *active_actor_selection(slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_editor_selection selection;
    SDL_zero(selection);
    if (runtime == NULL || !slayer3d_game_data_get_active_editor_selection(runtime, &selection) || !selection.hit ||
        selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR)
    {
        return NULL;
    }
    return editor_actor_for_name(runtime, selection.element_name);
}

bool slayer3d_game_data_editor_actor_locked(const slayer3d_game_data_runtime *runtime, const char *name)
{
    editor_actor_runtime *actor = editor_actor_for_name((slayer3d_game_data_runtime *)runtime, name);
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
            editor_brush_source_box_runtime *box =
                source_box_for_selection(mutable_runtime, &runtime->editor_selected_brushes[i]);
            if (box != NULL && box->locked)
                return true;
        }
    }

    editor_actor_runtime *actor = active_actor_selection(mutable_runtime);
    return actor != NULL && actor->locked;
}

void publish_editor_lock_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int locked_count = count_locked_editor_objects(runtime);
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
    editor_actor_runtime *actor = active_actor_selection(runtime);
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
        editor_brush_source_box_runtime *box = source_box_for_selection(runtime, &runtime->editor_selected_brushes[i]);
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

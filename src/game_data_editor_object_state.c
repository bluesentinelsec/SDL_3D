/**
 * @file game_data_editor_object_state.c
 * @brief Shared lookup and iteration helpers for editor-only object state.
 *
 * Lock and visibility toggles both need to find the actors and brush source
 * boxes behind the current editor selection, count flagged objects, and
 * rebuild the brush worlds they mutated. This module owns those lookups so the
 * lock and visibility modules cannot drift apart.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

bool editor_selection_is_brush(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
}

bool editor_selection_is_actor(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR &&
           selection->element_name != NULL && selection->element_name[0] != '\0';
}

const char *editor_selection_brush_identity(const slayer3d_game_data_editor_selection *selection)
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

const slayer3d_game_data_editor_selection *editor_runtime_active_selection_direct(
    const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !editor_selection_active_for_scene(runtime) || !runtime->editor_active_selection.hit)
        return NULL;
    return &runtime->editor_active_selection;
}

/* Returns mutable state from a const runtime (strchr-style) so read-only
 * queries and mutation paths can share one lookup. */
editor_actor_runtime *editor_runtime_find_actor(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL || name[0] == '\0')
        return NULL;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        editor_actor_runtime *actor = &((slayer3d_game_data_runtime *)runtime)->editor_actors[i];
        if (actor->name != NULL && SDL_strcmp(actor->name, name) == 0)
            return actor;
    }
    return NULL;
}

editor_actor_runtime *editor_runtime_active_selected_actor(const slayer3d_game_data_runtime *runtime)
{
    const slayer3d_game_data_editor_selection *selection = editor_runtime_active_selection_direct(runtime);
    if (!editor_selection_is_actor(selection))
        return NULL;
    return editor_runtime_find_actor(runtime, selection->element_name);
}

editor_brush_source_box_runtime *editor_runtime_find_source_box_for_selection(
    slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *selection, int *out_world_index)
{
    if (out_world_index != NULL)
        *out_world_index = -1;
    if (runtime == NULL || !editor_selection_is_brush(selection) || selection->world_name == NULL)
        return NULL;

    brush_world_runtime *world = find_brush_world_runtime_mutable(runtime, selection->world_name);
    if (world == NULL || !world->editor_has_source_model)
        return NULL;

    int source_index = editor_brush_world_find_source_box_index(world, editor_selection_brush_identity(selection));
    if (source_index < 0 && selection->element_name != NULL)
        source_index = editor_brush_world_find_source_box_index(world, selection->element_name);
    if (source_index < 0 || source_index >= world->editor_source_box_count)
        return NULL;

    if (out_world_index != NULL)
        *out_world_index = (int)(world - runtime->brush_worlds);
    return &world->editor_source_boxes[source_index];
}

static bool source_box_has_state(const editor_brush_source_box_runtime *box, editor_object_state_flag flag)
{
    return flag == EDITOR_OBJECT_STATE_LOCKED ? box->locked : box->hidden;
}

static bool actor_has_state(const editor_actor_runtime *actor, editor_object_state_flag flag)
{
    return flag == EDITOR_OBJECT_STATE_LOCKED ? actor->locked : actor->hidden;
}

int editor_runtime_count_objects_with_state(const slayer3d_game_data_runtime *runtime, editor_object_state_flag flag)
{
    if (runtime == NULL)
        return 0;

    int count = 0;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int i = 0; i < world->editor_source_box_count; ++i)
        {
            if (source_box_has_state(&world->editor_source_boxes[i], flag))
                ++count;
        }
    }
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (actor_has_state(&runtime->editor_actors[i], flag))
            ++count;
    }
    return count;
}

/* changed_worlds must hold runtime->brush_world_count entries. */
bool editor_runtime_rebuild_changed_brush_worlds(slayer3d_game_data_runtime *runtime, const bool *changed_worlds)
{
    if (runtime == NULL)
        return false;
    if (changed_worlds == NULL)
        return true;

    bool ok = true;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        if (changed_worlds[world_index] &&
            !editor_brush_world_rebuild_from_source(&runtime->brush_worlds[world_index], NULL, 0))
        {
            ok = false;
        }
    }
    return ok;
}

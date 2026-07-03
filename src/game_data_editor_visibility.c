/**
 * @file game_data_editor_visibility.c
 * @brief Editor-only visibility toggles for brushes and Things.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static int count_hidden_editor_objects(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return 0;

    int count = 0;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int i = 0; i < world->editor_source_box_count; ++i)
        {
            if (world->editor_source_boxes[i].hidden)
                ++count;
        }
    }
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (runtime->editor_actors[i].hidden)
            ++count;
    }
    return count;
}

void publish_editor_visibility_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int hidden_count = count_hidden_editor_objects(runtime);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.hidden_count", hidden_count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.has_hidden", hidden_count > 0);
}

static bool selection_is_active_actor(const slayer3d_game_data_runtime *runtime,
                                      slayer3d_game_data_editor_selection *out_selection)
{
    if (runtime == NULL || out_selection == NULL)
        return false;
    SDL_zero(*out_selection);
    return slayer3d_game_data_get_active_editor_selection(runtime, out_selection) && out_selection->hit &&
           out_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR && out_selection->element_name != NULL &&
           out_selection->element_name[0] != '\0';
}

static bool hide_selected_actor(slayer3d_game_data_runtime *runtime)
{
    slayer3d_game_data_editor_selection selection;
    if (!selection_is_active_actor(runtime, &selection))
        return false;

    editor_actor_runtime *actor = NULL;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (runtime->editor_actors[i].name != NULL &&
            SDL_strcmp(runtime->editor_actors[i].name, selection.element_name) == 0)
        {
            actor = &runtime->editor_actors[i];
            break;
        }
    }
    if (actor == NULL || actor->hidden)
        return false;
    actor->hidden = true;
    return true;
}

static bool hide_selected_brushes(slayer3d_game_data_runtime *runtime, int *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        runtime->editor_selected_brush_count <= 0)
        return true;

    bool ok = true;
    bool *world_changed = runtime->brush_world_count > 0
                              ? (bool *)SDL_calloc((size_t)runtime->brush_world_count, sizeof(*world_changed))
                              : NULL;
    if (runtime->brush_world_count > 0 && world_changed == NULL)
        return false;
    int hidden_count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *selection = &runtime->editor_selected_brushes[i];
        if (!selection->hit || selection->world_name == NULL)
            continue;
        brush_world_runtime *world = find_brush_world_runtime_mutable(runtime, selection->world_name);
        if (world == NULL || !world->editor_has_source_model)
            continue;
        const char *stable_id = selection->element_editor != NULL && selection->element_editor->stable_id != NULL
                                    ? selection->element_editor->stable_id
                                    : NULL;
        int source_index = editor_brush_world_find_source_box_index(world, stable_id);
        if (source_index < 0)
            source_index = editor_brush_world_find_source_box_index(world, selection->element_name);
        if (source_index < 0 || world->editor_source_boxes[source_index].hidden)
            continue;
        world->editor_source_boxes[source_index].hidden = true;
        ++hidden_count;
        const int world_index = (int)(world - runtime->brush_worlds);
        if (world_index >= 0 && world_index < runtime->brush_world_count)
            world_changed[world_index] = true;
    }

    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        if (world_changed[world_index] &&
            !editor_brush_world_rebuild_from_source(&runtime->brush_worlds[world_index], NULL, 0))
        {
            ok = false;
        }
    }
    SDL_free(world_changed);

    if (out_count != NULL)
        *out_count = hidden_count;
    return ok;
}

bool slayer3d_game_data_hide_selected_editor_objects_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const bool actor_hidden = hide_selected_actor(runtime);
    int hidden_brushes = 0;
    const bool brushes_ok = hide_selected_brushes(runtime, &hidden_brushes);
    const int total_hidden = hidden_brushes + (actor_hidden ? 1 : 0);

    if (total_hidden > 0)
    {
        (void)slayer3d_game_data_clear_active_editor_selection(runtime);
        char message[96];
        SDL_snprintf(message, sizeof(message), "hid %d selected object%s", total_hidden, total_hidden == 1 ? "" : "s");
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    }
    else
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "nothing selected to hide");
    }
    publish_editor_visibility_state(runtime);
    return brushes_ok;
}

bool slayer3d_game_data_show_all_editor_objects_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    int shown_count = 0;
    bool *world_changed = runtime->brush_world_count > 0
                              ? (bool *)SDL_calloc((size_t)runtime->brush_world_count, sizeof(*world_changed))
                              : NULL;
    if (runtime->brush_world_count > 0 && world_changed == NULL)
        return false;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int i = 0; i < world->editor_source_box_count; ++i)
        {
            if (!world->editor_source_boxes[i].hidden)
                continue;
            world->editor_source_boxes[i].hidden = false;
            ++shown_count;
            world_changed[world_index] = true;
        }
    }
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (!runtime->editor_actors[i].hidden)
            continue;
        runtime->editor_actors[i].hidden = false;
        ++shown_count;
    }

    bool ok = true;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        if (world_changed[world_index] &&
            !editor_brush_world_rebuild_from_source(&runtime->brush_worlds[world_index], NULL, 0))
        {
            ok = false;
        }
    }
    SDL_free(world_changed);

    char message[96];
    if (shown_count > 0)
        SDL_snprintf(message, sizeof(message), "showed %d hidden object%s", shown_count, shown_count == 1 ? "" : "s");
    else
        SDL_strlcpy(message, "no hidden objects", sizeof(message));
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    publish_editor_selected_brush_count(runtime);
    publish_editor_visibility_state(runtime);
    return ok;
}

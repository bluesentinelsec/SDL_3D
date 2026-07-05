/**
 * @file game_data_editor_visibility.c
 * @brief Editor-only visibility toggles for brushes and Things.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

void publish_editor_visibility_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int hidden_count = editor_runtime_count_objects_with_state(runtime, EDITOR_OBJECT_STATE_HIDDEN);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.hidden_count", hidden_count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.has_hidden", hidden_count > 0);
}

static bool hide_selected_actor(slayer3d_game_data_runtime *runtime)
{
    editor_actor_runtime *actor = editor_runtime_active_selected_actor(runtime);
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

    bool *world_changed = runtime->brush_world_count > 0
                              ? (bool *)SDL_calloc((size_t)runtime->brush_world_count, sizeof(*world_changed))
                              : NULL;
    if (runtime->brush_world_count > 0 && world_changed == NULL)
        return false;

    int hidden_count = 0;
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        int world_index = -1;
        editor_brush_source_box_runtime *box =
            editor_runtime_find_source_box_for_selection(runtime, &runtime->editor_selected_brushes[i], &world_index);
        if (box == NULL || box->hidden)
            continue;
        box->hidden = true;
        ++hidden_count;
        if (world_index >= 0 && world_index < runtime->brush_world_count)
            world_changed[world_index] = true;
    }

    const bool ok = editor_runtime_rebuild_changed_brush_worlds(runtime, world_changed);
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

    const bool ok = editor_runtime_rebuild_changed_brush_worlds(runtime, world_changed);
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

/**
 * @file game_data_editor_selection_helpers.c
 * @brief Shared editor selection helper functions.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_stdinc.h>

static bool editor_selection_additive_modifier_active(void)
{
    return (SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

bool editor_selection_is_selectable_brush(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->world_name[0] != '\0' && selection->element_name != NULL &&
           selection->element_name[0] != '\0';
}

bool editor_selection_from_brush_index(const slayer3d_game_data_runtime *runtime, const char *world_name,
                                       int brush_index, int face_index,
                                       slayer3d_game_data_editor_selection *out_selection)
{
    if (out_selection == NULL)
        return false;
    init_editor_selection(out_selection);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || brush_index < 0)
        return false;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    if (brush_index >= world->brush_count)
        return false;

    const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
    if (brush->name == NULL || brush->name[0] == '\0')
        return false;

    out_selection->hit = true;
    out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
    out_selection->world_name = world->name;
    out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    out_selection->element_name = brush->name;
    out_selection->element_index = brush_index;
    out_selection->face_index = face_index >= 0 && face_index < brush->face_count ? face_index : -1;
    out_selection->has_bounds = brush->has_bounds;
    if (brush->has_bounds)
        out_selection->bounds = brush->bounds;
    return true;
}

int editor_selected_brush_index(const slayer3d_game_data_runtime *runtime,
                                const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        !editor_selection_is_selectable_brush(selection))
    {
        return -1;
    }
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        const slayer3d_game_data_editor_selection *candidate = &runtime->editor_selected_brushes[i];
        if (editor_selection_is_selectable_brush(candidate) &&
            SDL_strcmp(candidate->world_name, selection->world_name) == 0 &&
            SDL_strcmp(candidate->element_name, selection->element_name) == 0)
        {
            return i;
        }
    }
    return -1;
}

void remove_editor_selected_brush_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_brush_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_brush_count; ++i)
        runtime->editor_selected_brushes[i] = runtime->editor_selected_brushes[i + 1];
    runtime->editor_selected_brush_count--;
    init_editor_selection(&runtime->editor_selected_brushes[runtime->editor_selected_brush_count]);
    publish_editor_selected_brush_count(runtime);
}

bool add_editor_selected_brush(slayer3d_game_data_runtime *runtime,
                               const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || !editor_selection_is_selectable_brush(selection) ||
        runtime->editor_selected_brush_count >= SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY)
    {
        return false;
    }
    if (!editor_selected_brushes_active_for_scene(runtime))
        clear_editor_selected_brushes(runtime);
    runtime->editor_selected_brushes[runtime->editor_selected_brush_count++] = *selection;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
    return true;
}

void update_active_editor_selection_from_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    if (editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0)
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
    else
        init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
}

bool editor_select_mode_primary_click(slayer3d_game_data_runtime *runtime,
                                      const slayer3d_game_data_editor_selection *hover_selection)
{
    const bool additive = editor_selection_additive_modifier_active();
    if (!editor_selection_is_selectable_brush(hover_selection))
    {
        if (hover_selection != NULL && hover_selection->hit &&
            hover_selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START)
        {
            clear_editor_selected_brushes(runtime);
            runtime->editor_active_selection = *hover_selection;
            runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
            return true;
        }
        if (!additive)
            clear_editor_active_selection(runtime);
        return true;
    }

    const int selected_index = editor_selected_brush_index(runtime, hover_selection);
    if (selected_index >= 0)
    {
        remove_editor_selected_brush_at(runtime, selected_index);
        update_active_editor_selection_from_selected_brushes(runtime);
        return true;
    }

    if (!additive)
        clear_editor_selected_brushes(runtime);
    if (!add_editor_selected_brush(runtime, hover_selection))
        return false;
    update_active_editor_selection_from_selected_brushes(runtime);
    return true;
}

bool editor_select_mode_secondary_click(slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_editor_selection *hover_selection)
{
    if (editor_selection_is_selectable_brush(hover_selection) &&
        editor_selected_brush_index(runtime, hover_selection) < 0)
    {
        clear_editor_selected_brushes(runtime);
        if (!add_editor_selected_brush(runtime, hover_selection))
            return false;
        update_active_editor_selection_from_selected_brushes(runtime);
        return true;
    }

    if (!editor_selection_is_selectable_brush(hover_selection) && hover_selection != NULL && hover_selection->hit)
    {
        clear_editor_selected_brushes(runtime);
        runtime->editor_active_selection = *hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }
    return true;
}

bool editor_mode_is_select(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "select") == 0;
}

bool editor_mode_is_brush(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "brush") == 0;
}

bool editor_mode_is_face(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "face") == 0;
}

bool editor_mode_is_edge(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "edge") == 0;
}

bool editor_mode_is_vertex(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "vertex") == 0;
}

bool editor_mode_is_rotate(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "rotate") == 0;
}

bool editor_mode_is_scale(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "scale") == 0;
}

bool editor_mode_is_shear(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "shear") == 0;
}

bool editor_mode_is_paint(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           (SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "paint") == 0 ||
            SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.tool.mode", "select"), "paint") ==
                0);
}

bool editor_selection_matches_brush(const slayer3d_game_data_editor_selection *selection, const char *world_name,
                                    const char *element_name)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->element_name != NULL && world_name != NULL &&
           element_name != NULL && SDL_strcmp(selection->world_name, world_name) == 0 &&
           SDL_strcmp(selection->element_name, element_name) == 0;
}

bool editor_hover_is_selected_brush(const slayer3d_game_data_runtime *runtime,
                                    const slayer3d_game_data_editor_selection *hover_selection)
{
    if (runtime == NULL || hover_selection == NULL ||
        hover_selection->type != SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
    {
        return false;
    }
    for (int i = 0; i < runtime->editor_selected_brush_count; ++i)
    {
        if (editor_selection_matches_brush(&runtime->editor_selected_brushes[i], hover_selection->world_name,
                                           hover_selection->element_name))
        {
            return true;
        }
    }
    return false;
}

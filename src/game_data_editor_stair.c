/**
 * @file game_data_editor_stair.c
 * @brief Editor stair-brush metadata and mutation actions.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#define EDITOR_STAIR_ROOT_KEY "editor.stair.root"
#define EDITOR_STAIR_INDEX_KEY "editor.stair.index"
#define EDITOR_STAIR_ASCENDING_KEY "editor.stair.ascending"
#define EDITOR_STAIR_RUN_DELTA_KEY "editor.stair.run_delta"
#define EDITOR_STAIR_RISE_DELTA_KEY "editor.stair.rise_delta"
#define EDITOR_STAIR_GIZMO_VISIBLE_KEY "editor.stair.gizmo.visible"
#define EDITOR_STAIR_GIZMO_DIRECTION_KEY "editor.stair.gizmo.direction"
#define EDITOR_STAIR_GIZMO_ADD_KEY "editor.stair.gizmo.add"
#define EDITOR_STAIR_GIZMO_REMOVE_KEY "editor.stair.gizmo.remove"
#define EDITOR_STAIR_GIZMO_RUN_KEY "editor.stair.gizmo.run"
#define EDITOR_STAIR_GIZMO_SIDE_KEY "editor.stair.gizmo.side"
#define EDITOR_STAIR_MAX_STEPS 64

static bool editor_source_box_is_stair(const editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return false;
    if (box->prefab != NULL && SDL_strcmp(box->prefab, "stairs") == 0)
        return true;
    const char *root =
        box->properties != NULL ? slayer3d_properties_get_string(box->properties, EDITOR_STAIR_ROOT_KEY, "") : "";
    return root != NULL && root[0] != '\0';
}

static bool editor_stair_ensure_properties(editor_brush_source_box_runtime *box)
{
    if (box == NULL)
        return false;
    if (box->properties != NULL)
        return true;
    box->properties = slayer3d_properties_create();
    return box->properties != NULL;
}

static slayer3d_vec3 editor_stair_default_run_delta(const editor_brush_source_box_runtime *box)
{
    const int depth = box != NULL ? SDL_max(1, box->max[2] - box->min[2]) : 1;
    return slayer3d_vec3_make(0.0f, 0.0f, (float)depth);
}

static slayer3d_vec3 editor_stair_default_rise_delta(const editor_brush_source_box_runtime *box)
{
    const int height = box != NULL ? SDL_max(1, box->max[1] - box->min[1]) : 1;
    return slayer3d_vec3_make(0.0f, (float)height, 0.0f);
}

static const char *editor_stair_root_id(const editor_brush_source_box_runtime *box)
{
    const char *root = box != NULL && box->properties != NULL
                           ? slayer3d_properties_get_string(box->properties, EDITOR_STAIR_ROOT_KEY, NULL)
                           : NULL;
    if (root != NULL && root[0] != '\0')
        return root;
    return box != NULL ? box->stable_id : NULL;
}

static int editor_stair_index(const editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->properties == NULL)
        return 0;
    return slayer3d_properties_get_int(box->properties, EDITOR_STAIR_INDEX_KEY, 0);
}

static bool editor_stair_ascending(const editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->properties == NULL)
        return true;
    return slayer3d_properties_get_bool(box->properties, EDITOR_STAIR_ASCENDING_KEY, true);
}

static slayer3d_vec3 editor_stair_run_delta(const editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->properties == NULL)
        return editor_stair_default_run_delta(box);
    return slayer3d_properties_get_vec3(box->properties, EDITOR_STAIR_RUN_DELTA_KEY,
                                        editor_stair_default_run_delta(box));
}

static slayer3d_vec3 editor_stair_rise_delta(const editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->properties == NULL)
        return editor_stair_default_rise_delta(box);
    return slayer3d_properties_get_vec3(box->properties, EDITOR_STAIR_RISE_DELTA_KEY,
                                        editor_stair_default_rise_delta(box));
}

static void editor_stair_set_common_metadata(editor_brush_source_box_runtime *box, const char *root_id, int index,
                                             bool ascending, slayer3d_vec3 run_delta, slayer3d_vec3 rise_delta)
{
    if (!editor_stair_ensure_properties(box))
        return;
    slayer3d_properties_set_string(box->properties, EDITOR_STAIR_ROOT_KEY, root_id != NULL ? root_id : "");
    slayer3d_properties_set_int(box->properties, EDITOR_STAIR_INDEX_KEY, index);
    slayer3d_properties_set_bool(box->properties, EDITOR_STAIR_ASCENDING_KEY, ascending);
    slayer3d_properties_set_vec3(box->properties, EDITOR_STAIR_RUN_DELTA_KEY, run_delta);
    slayer3d_properties_set_vec3(box->properties, EDITOR_STAIR_RISE_DELTA_KEY, rise_delta);
}

static bool editor_stair_ensure_root_metadata(editor_brush_source_box_runtime *box)
{
    if (box == NULL || box->stable_id == NULL || box->stable_id[0] == '\0')
        return false;
    const bool ascending = editor_stair_ascending(box);
    editor_stair_set_common_metadata(box, box->stable_id, 0, ascending, editor_stair_run_delta(box),
                                     editor_stair_rise_delta(box));
    return box->properties != NULL;
}

static bool editor_stair_root_matches(const editor_brush_source_box_runtime *box, const char *root_id)
{
    const char *box_root = editor_stair_root_id(box);
    return box_root != NULL && root_id != NULL && SDL_strcmp(box_root, root_id) == 0;
}

static bool editor_stair_resolve_selected_root(slayer3d_game_data_runtime *runtime, brush_world_runtime **out_world,
                                               int *out_world_index, int *out_root_index,
                                               editor_brush_source_box_runtime **out_root)
{
    if (out_world != NULL)
        *out_world = NULL;
    if (out_world_index != NULL)
        *out_world_index = -1;
    if (out_root_index != NULL)
        *out_root_index = -1;
    if (out_root != NULL)
        *out_root = NULL;
    if (runtime == NULL || !editor_selected_brushes_active_for_scene(runtime) ||
        runtime->editor_selected_brush_count <= 0)
    {
        return false;
    }

    int world_index = -1;
    editor_brush_source_box_runtime *selected =
        editor_runtime_find_source_box_for_selection(runtime, &runtime->editor_selected_brushes[0], &world_index);
    if (!editor_source_box_is_stair(selected) || world_index < 0 || world_index >= runtime->brush_world_count)
        return false;

    brush_world_runtime *world = &runtime->brush_worlds[world_index];
    const char *root_id = editor_stair_root_id(selected);
    if (root_id == NULL || root_id[0] == '\0')
        return false;

    int root_index = editor_brush_world_find_source_box_index(world, root_id);
    if (root_index < 0)
        return false;

    editor_brush_source_box_runtime *root = &world->editor_source_boxes[root_index];
    if (!editor_source_box_is_stair(root))
        return false;
    if (!editor_stair_ensure_root_metadata(root))
        return false;

    if (out_world != NULL)
        *out_world = world;
    if (out_world_index != NULL)
        *out_world_index = world_index;
    if (out_root_index != NULL)
        *out_root_index = root_index;
    if (out_root != NULL)
        *out_root = root;
    return true;
}

static int editor_stair_step_count(const brush_world_runtime *world, const char *root_id, int *out_max_index,
                                   int *out_max_source_index)
{
    if (out_max_index != NULL)
        *out_max_index = 0;
    if (out_max_source_index != NULL)
        *out_max_source_index = -1;
    if (world == NULL || root_id == NULL || root_id[0] == '\0')
        return 0;

    int count = 0;
    int max_index = -1;
    int max_source_index = -1;
    for (int i = 0; i < world->editor_source_box_count; ++i)
    {
        const editor_brush_source_box_runtime *box = &world->editor_source_boxes[i];
        if (!editor_source_box_is_stair(box) || !editor_stair_root_matches(box, root_id))
            continue;
        const int index = editor_stair_index(box);
        ++count;
        if (index > max_index)
        {
            max_index = index;
            max_source_index = i;
        }
    }
    if (out_max_index != NULL)
        *out_max_index = max_index;
    if (out_max_source_index != NULL)
        *out_max_source_index = max_source_index;
    return count;
}

static void editor_stair_translate_box_source(editor_brush_source_box_runtime *box, slayer3d_vec3 offset)
{
    if (box == NULL)
        return;
    const int delta[3] = {(int)SDL_lroundf(offset.x), (int)SDL_lroundf(offset.y), (int)SDL_lroundf(offset.z)};
    for (int axis = 0; axis < 3; ++axis)
    {
        box->min[axis] += delta[axis];
        box->max[axis] += delta[axis];
    }
    for (int vertex = 0; vertex < box->vertex_count; ++vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
            box->vertices[vertex][axis] += delta[axis];
    }
}

static void editor_stair_step_name(const char *root_id, int step_index, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u)
        return;
    SDL_snprintf(buffer, buffer_size, "%s.step.%02d", root_id != NULL ? root_id : "stair", step_index);
}

static void editor_stair_publish_message(slayer3d_game_data_runtime *runtime, const char *message)
{
    if (runtime != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message != NULL ? message : "");
    publish_editor_stair_state(runtime);
}

static void editor_stair_reselect_root(slayer3d_game_data_runtime *runtime, const brush_world_runtime *world,
                                       const char *root_id)
{
    if (runtime == NULL || world == NULL || root_id == NULL || root_id[0] == '\0')
        return;

    slayer3d_game_data_editor_selection selection;
    init_editor_selection(&selection);
    refresh_editor_brush_selection_for_identity(world, &selection, root_id, root_id, -1, NULL);
    if (!selection.hit)
        return;

    clear_editor_selected_brushes(runtime);
    (void)add_editor_selected_brush(runtime, &selection);
    update_active_editor_selection_from_selected_brushes(runtime);
}

static void editor_stair_clear_gizmo_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    slayer3d_properties_set_bool(runtime->scene_state, EDITOR_STAIR_GIZMO_VISIBLE_KEY, false);
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_DIRECTION_KEY,
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_ADD_KEY,
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_REMOVE_KEY,
                                 slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_RUN_KEY,
                                 slayer3d_vec3_make(0.0f, 0.0f, 1.0f));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_SIDE_KEY,
                                 slayer3d_vec3_make(1.0f, 0.0f, 0.0f));
    slayer3d_properties_set_string(runtime->scene_state, "editor.stair.gizmo.hover", "");
}

static void editor_stair_publish_gizmo_state(slayer3d_game_data_runtime *runtime, const brush_world_runtime *world,
                                             const editor_brush_source_box_runtime *root)
{
    if (runtime == NULL || runtime->scene_state == NULL || world == NULL || root == NULL)
        return;

    slayer3d_vec3 run = editor_stair_run_delta(root);
    run.y = 0.0f;
    const float run_len_sq = run.x * run.x + run.z * run.z;
    if (run_len_sq <= 0.000001f)
        run = slayer3d_vec3_make(0.0f, 0.0f, 1.0f);
    else
        run = slayer3d_vec3_scale(run, 1.0f / SDL_sqrtf(run_len_sq));

    slayer3d_vec3 side = slayer3d_vec3_make(run.z, 0.0f, -run.x);
    const float side_len_sq = side.x * side.x + side.z * side.z;
    if (side_len_sq <= 0.000001f)
        side = slayer3d_vec3_make(1.0f, 0.0f, 0.0f);
    else
        side = slayer3d_vec3_scale(side, 1.0f / SDL_sqrtf(side_len_sq));

    const slayer3d_bounding_box bounds = editor_brush_source_box_bounds_meters(world, root);
    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    const slayer3d_vec3 half = slayer3d_vec3_scale(slayer3d_vec3_sub(bounds.max, bounds.min), 0.5f);
    const float run_extent = SDL_fabsf(run.x) * half.x + SDL_fabsf(run.z) * half.z;
    const float base_size = SDL_max(SDL_max(half.x, half.z), 0.25f);
    const float outward = SDL_clamp(base_size * 0.35f, 0.2f, 0.65f);
    const float spacing = SDL_clamp(base_size * 0.5f, 0.35f, 0.9f);
    const float lift = SDL_clamp(SDL_max(bounds.max.y - bounds.min.y, 0.25f) * 0.25f, 0.15f, 0.4f);
    slayer3d_vec3 anchor = slayer3d_vec3_add(center, slayer3d_vec3_scale(run, run_extent + outward));
    anchor.y = bounds.max.y + lift;

    slayer3d_properties_set_bool(runtime->scene_state, EDITOR_STAIR_GIZMO_VISIBLE_KEY, true);
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_DIRECTION_KEY,
                                 slayer3d_vec3_sub(anchor, slayer3d_vec3_scale(side, spacing)));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_ADD_KEY, anchor);
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_REMOVE_KEY,
                                 slayer3d_vec3_add(anchor, slayer3d_vec3_scale(side, spacing)));
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_RUN_KEY, run);
    slayer3d_properties_set_vec3(runtime->scene_state, EDITOR_STAIR_GIZMO_SIDE_KEY, side);
}

void publish_editor_stair_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    brush_world_runtime *world = NULL;
    editor_brush_source_box_runtime *root = NULL;
    const bool selected = editor_stair_resolve_selected_root(runtime, &world, NULL, NULL, &root);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.stair.selected", selected);
    if (!selected || root == NULL)
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.stair.direction_label", "");
        slayer3d_properties_set_bool(runtime->scene_state, "editor.stair.ascending", true);
        slayer3d_properties_set_int(runtime->scene_state, "editor.stair.step_count", 0);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.stair.can_remove_step", false);
        editor_stair_clear_gizmo_state(runtime);
        return;
    }

    int max_index = 0;
    const int count = editor_stair_step_count(world, root->stable_id, &max_index, NULL);
    slayer3d_properties_set_string(runtime->scene_state, "editor.stair.direction_label",
                                   editor_stair_ascending(root) ? "Up" : "Down");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.stair.ascending", editor_stair_ascending(root));
    slayer3d_properties_set_int(runtime->scene_state, "editor.stair.step_count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.stair.can_remove_step", max_index > 0);
    editor_stair_publish_gizmo_state(runtime, world, root);
}

bool slayer3d_game_data_toggle_selected_editor_stair_direction_action(slayer3d_game_data_runtime *runtime,
                                                                      yyjson_val *action)
{
    (void)action;
    brush_world_runtime *world = NULL;
    editor_brush_source_box_runtime *root = NULL;
    if (!editor_stair_resolve_selected_root(runtime, &world, NULL, NULL, &root))
    {
        editor_stair_publish_message(runtime, "select a stair brush before changing stair direction");
        return true;
    }

    const char *root_id = root->stable_id;
    const bool ascending = !editor_stair_ascending(root);
    const slayer3d_vec3 run_delta = editor_stair_run_delta(root);
    const slayer3d_vec3 rise_delta = editor_stair_rise_delta(root);
    for (int i = 0; i < world->editor_source_box_count; ++i)
    {
        editor_brush_source_box_runtime *box = &world->editor_source_boxes[i];
        if (editor_source_box_is_stair(box) && editor_stair_root_matches(box, root_id))
            editor_stair_set_common_metadata(box, root_id, editor_stair_index(box), ascending, run_delta, rise_delta);
    }

    editor_stair_publish_message(runtime, ascending ? "stair direction set to up" : "stair direction set to down");
    return true;
}

bool slayer3d_game_data_add_selected_editor_stair_step_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    brush_world_runtime *world = NULL;
    editor_brush_source_box_runtime *root = NULL;
    if (!editor_stair_resolve_selected_root(runtime, &world, NULL, NULL, &root))
    {
        editor_stair_publish_message(runtime, "select a stair brush before adding a step");
        return true;
    }

    int max_index = 0;
    const int count = editor_stair_step_count(world, root->stable_id, &max_index, NULL);
    if (count >= EDITOR_STAIR_MAX_STEPS)
    {
        editor_stair_publish_message(runtime, "stair brush already has the maximum number of steps");
        return true;
    }

    const int step_index = SDL_max(max_index + 1, 1);
    char root_id[256];
    SDL_strlcpy(root_id, root->stable_id != NULL ? root->stable_id : "", sizeof(root_id));
    editor_brush_source_box_runtime step;
    if (!copy_editor_brush_source_box_runtime(root, &step))
    {
        editor_stair_publish_message(runtime, "failed to allocate stair step");
        return false;
    }

    char step_id[256];
    editor_stair_step_name(root_id, step_index, step_id, sizeof(step_id));
    SDL_free(step.stable_id);
    SDL_free(step.name);
    step.stable_id = SDL_strdup(step_id);
    step.name = SDL_strdup(step_id);
    if (step.stable_id == NULL || step.name == NULL)
    {
        free_editor_brush_source_box_runtime(&step);
        editor_stair_publish_message(runtime, "failed to allocate stair step identity");
        return false;
    }

    const slayer3d_vec3 run_delta = editor_stair_run_delta(root);
    const slayer3d_vec3 rise_delta = editor_stair_rise_delta(root);
    const float rise_sign = editor_stair_ascending(root) ? 1.0f : -1.0f;
    const slayer3d_vec3 offset = slayer3d_vec3_add(slayer3d_vec3_scale(run_delta, (float)step_index),
                                                   slayer3d_vec3_scale(rise_delta, rise_sign * (float)step_index));
    editor_stair_translate_box_source(&step, offset);
    editor_stair_set_common_metadata(&step, root_id, step_index, editor_stair_ascending(root), run_delta, rise_delta);
    step.hidden = false;

    char error_buffer[256] = {0};
    const bool ok = editor_brush_world_insert_source_box_at_index(world, world->editor_source_box_count, &step,
                                                                  error_buffer, sizeof(error_buffer));
    free_editor_brush_source_box_runtime(&step);
    if (!ok)
    {
        editor_stair_publish_message(runtime, error_buffer[0] != '\0' ? error_buffer : "failed to add stair step");
        return false;
    }

    editor_stair_reselect_root(runtime, world, root_id);
    char message[96];
    SDL_snprintf(message, sizeof(message), "added stair step %d", step_index);
    editor_stair_publish_message(runtime, message);
    return true;
}

bool slayer3d_game_data_remove_selected_editor_stair_step_action(slayer3d_game_data_runtime *runtime,
                                                                 yyjson_val *action)
{
    (void)action;
    brush_world_runtime *world = NULL;
    editor_brush_source_box_runtime *root = NULL;
    if (!editor_stair_resolve_selected_root(runtime, &world, NULL, NULL, &root))
    {
        editor_stair_publish_message(runtime, "select a stair brush before removing a step");
        return true;
    }

    int max_index = 0;
    int max_source_index = -1;
    char root_id[256];
    SDL_strlcpy(root_id, root->stable_id != NULL ? root->stable_id : "", sizeof(root_id));
    (void)editor_stair_step_count(world, root->stable_id, &max_index, &max_source_index);
    if (max_index <= 0 || max_source_index < 0)
    {
        editor_stair_publish_message(runtime, "stair brush base step cannot be removed");
        return true;
    }

    char error_buffer[256] = {0};
    if (!editor_brush_world_remove_source_box_at_index(world, max_source_index, error_buffer, sizeof(error_buffer)))
    {
        editor_stair_publish_message(runtime, error_buffer[0] != '\0' ? error_buffer : "failed to remove stair step");
        return false;
    }

    editor_stair_reselect_root(runtime, world, root_id);
    char message[96];
    SDL_snprintf(message, sizeof(message), "removed stair step %d", max_index);
    editor_stair_publish_message(runtime, message);
    return true;
}

/**
 * @file game_data_editor_visibility.c
 * @brief Editor-only visibility toggles for brushes and Things.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_editor_actions_internal.h"
#include "slayer3d/math.h"

#define EDITOR_VISIBILITY_HIDDEN_SLOT_COUNT 10

typedef enum editor_hidden_object_type
{
    EDITOR_HIDDEN_OBJECT_NONE,
    EDITOR_HIDDEN_OBJECT_BRUSH,
    EDITOR_HIDDEN_OBJECT_ACTOR,
} editor_hidden_object_type;

typedef struct editor_hidden_object_ref
{
    editor_hidden_object_type type;
    int world_index;
    int source_index;
    int actor_index;
    const char *world_name;
    const char *name;
    const char *label;
    slayer3d_bounding_box bounds;
    bool has_bounds;
} editor_hidden_object_ref;

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

static slayer3d_bounding_box editor_hidden_actor_bounds(const editor_actor_runtime *actor)
{
    const slayer3d_vec3 scale = actor != NULL ? actor->scale : slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    slayer3d_vec3 half = slayer3d_vec3_make(0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z);
    const char *mesh = actor != NULL ? actor->mesh : NULL;
    if (mesh != NULL && SDL_strcmp(mesh, "capsule") == 0)
        half = slayer3d_vec3_make(0.35f * scale.x, 0.9f * scale.y, 0.35f * scale.z);
    else if (mesh != NULL && SDL_strcmp(mesh, "rectangle") == 0)
        half = slayer3d_vec3_make(0.45f * scale.x, 0.9f * scale.y, 0.25f * scale.z);

    const slayer3d_vec3 position = actor != NULL ? actor->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    slayer3d_bounding_box bounds;
    bounds.min = slayer3d_vec3_make(position.x - half.x, position.y, position.z - half.z);
    bounds.max = slayer3d_vec3_make(position.x + half.x, position.y + half.y * 2.0f, position.z + half.z);
    return bounds;
}

static void clear_hidden_slot(slayer3d_properties *state, int slot)
{
    char key[128];
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.available", slot);
    slayer3d_properties_set_bool(state, key, false);
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.label", slot);
    slayer3d_properties_set_string(state, key, "");
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.type", slot);
    slayer3d_properties_set_string(state, key, "");
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.name", slot);
    slayer3d_properties_set_string(state, key, "");
}

static void publish_hidden_slot(slayer3d_properties *state, int slot, const editor_hidden_object_ref *ref)
{
    char key[128];
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.available", slot);
    slayer3d_properties_set_bool(state, key, true);
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.label", slot);
    slayer3d_properties_set_string(state, key, ref->label != NULL ? ref->label : "");
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.type", slot);
    slayer3d_properties_set_string(state, key, ref->type == EDITOR_HIDDEN_OBJECT_BRUSH ? "Brush" : "Thing");
    SDL_snprintf(key, sizeof(key), "editor.visibility.hidden.%d.name", slot);
    slayer3d_properties_set_string(state, key, ref->name != NULL ? ref->name : "");
}

static bool hidden_object_ref_for_slot(const slayer3d_game_data_runtime *runtime, int slot,
                                       editor_hidden_object_ref *out_ref)
{
    if (out_ref != NULL)
        SDL_zero(*out_ref);
    if (runtime == NULL || slot < 0 || out_ref == NULL)
        return false;

    int hidden_slot = 0;
    for (int world_index = 0; world_index < runtime->brush_world_count; ++world_index)
    {
        const brush_world_runtime *world = &runtime->brush_worlds[world_index];
        for (int source_index = 0; source_index < world->editor_source_box_count; ++source_index)
        {
            const editor_brush_source_box_runtime *box = &world->editor_source_boxes[source_index];
            if (!box->hidden)
                continue;
            if (hidden_slot++ != slot)
                continue;
            out_ref->type = EDITOR_HIDDEN_OBJECT_BRUSH;
            out_ref->world_index = world_index;
            out_ref->source_index = source_index;
            out_ref->world_name = world->desc.name;
            out_ref->name = box->stable_id != NULL && box->stable_id[0] != '\0' ? box->stable_id : box->name;
            out_ref->label = out_ref->name;
            out_ref->bounds = editor_brush_source_box_bounds_meters(world, box);
            out_ref->has_bounds = true;
            return true;
        }
    }

    for (int actor_index = 0; actor_index < runtime->editor_actor_count; ++actor_index)
    {
        const editor_actor_runtime *actor = &runtime->editor_actors[actor_index];
        if (!actor->hidden)
            continue;
        if (hidden_slot++ != slot)
            continue;
        out_ref->type = EDITOR_HIDDEN_OBJECT_ACTOR;
        out_ref->actor_index = actor_index;
        out_ref->name = actor->name;
        out_ref->label =
            actor->display_name != NULL && actor->display_name[0] != '\0' ? actor->display_name : actor->name;
        out_ref->bounds = editor_hidden_actor_bounds(actor);
        out_ref->has_bounds = true;
        return true;
    }
    return false;
}

void publish_editor_visibility_state(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;

    const int hidden_count = count_hidden_editor_objects(runtime);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.hidden_count", hidden_count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.has_hidden", hidden_count > 0);

    for (int i = 0; i < EDITOR_VISIBILITY_HIDDEN_SLOT_COUNT; ++i)
    {
        editor_hidden_object_ref ref;
        if (hidden_object_ref_for_slot(runtime, i, &ref))
            publish_hidden_slot(runtime->scene_state, i, &ref);
        else
            clear_hidden_slot(runtime->scene_state, i);
    }

    const int focused_slot = slayer3d_properties_get_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
    editor_hidden_object_ref focused;
    if (focused_slot < 0 || !hidden_object_ref_for_slot(runtime, focused_slot, &focused))
    {
        slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.focused.available", false);
        slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
        slayer3d_properties_set_string(runtime->scene_state, "editor.visibility.focused.label", "");
        slayer3d_properties_set_string(runtime->scene_state, "editor.visibility.focused.type", "");
    }
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

    int hidden_brushes = 0;
    const bool brushes_ok = hide_selected_brushes(runtime, &hidden_brushes);
    const bool actor_hidden = hide_selected_actor(runtime);
    const int total_hidden = hidden_brushes + (actor_hidden ? 1 : 0);

    if (total_hidden > 0)
    {
        (void)slayer3d_game_data_clear_active_editor_selection(runtime);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.focused.available", false);
        slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
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

static void focus_editor_camera_on_bounds(slayer3d_game_data_runtime *runtime, slayer3d_bounding_box bounds)
{
    slayer3d_registered_actor *camera = slayer3d_game_data_find_actor(runtime, "entity.editor_shell.camera");
    if (camera == NULL)
        return;

    const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(bounds.min, bounds.max), 0.5f);
    const slayer3d_vec3 size = slayer3d_vec3_sub(bounds.max, bounds.min);
    const float radius = SDL_max(SDL_max(SDL_fabsf(size.x), SDL_fabsf(size.y)), SDL_fabsf(size.z));
    const float distance = SDL_max(radius * 2.6f, 4.0f);
    const slayer3d_vec3 offset = slayer3d_vec3_make(-distance, SDL_max(distance * 0.45f, 2.0f), distance);
    camera->position = slayer3d_vec3_add(center, offset);

    const slayer3d_vec3 dir = slayer3d_vec3_normalize(slayer3d_vec3_sub(center, camera->position));
    const float yaw = SDL_atan2f(dir.x, -dir.z);
    const float pitch = SDL_asinf(SDL_clamp(dir.y, -0.98f, 0.98f));
    slayer3d_properties_set_float(camera->props, "yaw", yaw);
    slayer3d_properties_set_float(camera->props, "pitch", pitch);
    slayer3d_properties_set_vec3(camera->props, "camera_forward", dir);
    slayer3d_properties_set_string(runtime->scene_state, "editor.view.mode", "flyby_3d");
}

bool slayer3d_game_data_focus_hidden_editor_object_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const int slot = json_int(action, "slot", -1);
    editor_hidden_object_ref ref;
    if (!hidden_object_ref_for_slot(runtime, slot, &ref))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "hidden object unavailable");
        publish_editor_visibility_state(runtime);
        return true;
    }

    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.focused.available", true);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.focused.slot", slot);
    slayer3d_properties_set_string(runtime->scene_state, "editor.visibility.focused.label",
                                   ref.label != NULL ? ref.label : "");
    slayer3d_properties_set_string(runtime->scene_state, "editor.visibility.focused.type",
                                   ref.type == EDITOR_HIDDEN_OBJECT_BRUSH ? "Brush" : "Thing");
    if (ref.has_bounds)
    {
        const slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(ref.bounds.min, ref.bounds.max), 0.5f);
        slayer3d_properties_set_vec3(runtime->scene_state, "editor.visibility.focused.point", center);
        focus_editor_camera_on_bounds(runtime, ref.bounds);
    }
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "focused hidden object");
    publish_editor_visibility_state(runtime);
    return true;
}

bool slayer3d_game_data_show_focused_editor_object_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    (void)action;
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const int slot = slayer3d_properties_get_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
    editor_hidden_object_ref ref;
    if (!hidden_object_ref_for_slot(runtime, slot, &ref))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "hidden object unavailable");
        publish_editor_visibility_state(runtime);
        return true;
    }

    bool ok = true;
    if (ref.type == EDITOR_HIDDEN_OBJECT_BRUSH && ref.world_index >= 0 && ref.world_index < runtime->brush_world_count)
    {
        brush_world_runtime *world = &runtime->brush_worlds[ref.world_index];
        if (ref.source_index >= 0 && ref.source_index < world->editor_source_box_count)
        {
            world->editor_source_boxes[ref.source_index].hidden = false;
            ok = editor_brush_world_rebuild_from_source(world, NULL, 0);
        }
    }
    else if (ref.type == EDITOR_HIDDEN_OBJECT_ACTOR && ref.actor_index >= 0 &&
             ref.actor_index < runtime->editor_actor_count)
    {
        runtime->editor_actors[ref.actor_index].hidden = false;
    }

    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.focused.available", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "made hidden object visible");
    publish_editor_selected_brush_count(runtime);
    publish_editor_visibility_state(runtime);
    return ok;
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
    slayer3d_properties_set_bool(runtime->scene_state, "editor.visibility.focused.available", false);
    slayer3d_properties_set_int(runtime->scene_state, "editor.visibility.focused.slot", -1);
    publish_editor_selected_brush_count(runtime);
    publish_editor_visibility_state(runtime);
    return ok;
}

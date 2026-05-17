/**
 * @file game_data_editor_player_start.c
 * @brief Editor player-start placement, state, and test-run manifest helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include <stdlib.h>

static editor_player_start_runtime *find_editor_player_start_mutable(slayer3d_game_data_runtime *runtime,
                                                                     const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->editor_player_start_count; ++i)
    {
        if (runtime->editor_player_starts[i].name != NULL &&
            SDL_strcmp(runtime->editor_player_starts[i].name, name) == 0)
        {
            return &runtime->editor_player_starts[i];
        }
    }
    return NULL;
}

const editor_player_start_runtime *find_editor_player_start(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_editor_player_start_mutable((slayer3d_game_data_runtime *)runtime, name);
}

static bool runtime_scene_exists(const slayer3d_game_data_runtime *runtime, const char *scene)
{
    return scene == NULL || scene[0] == '\0' || find_scene_const(runtime, scene) != NULL;
}

bool slayer3d_game_data_get_editor_player_start(const slayer3d_game_data_runtime *runtime, const char *name,
                                                slayer3d_game_data_editor_player_start *out_start)
{
    if (out_start != NULL)
        SDL_zero(*out_start);
    const editor_player_start_runtime *start = find_editor_player_start(runtime, name);
    if (start == NULL || out_start == NULL)
        return false;
    out_start->name = start->name;
    out_start->scene = start->scene;
    out_start->target = start->target;
    out_start->position = start->position;
    out_start->yaw = start->yaw;
    out_start->pitch = start->pitch;
    return true;
}

bool slayer3d_game_data_get_player_start_editor_state(const slayer3d_game_data_runtime *runtime,
                                                      slayer3d_game_data_player_start_editor_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    if (runtime == NULL || out_state == NULL)
        return false;
    out_state->source_path = runtime->editor_player_start_source_path;
    out_state->dirty = runtime->editor_player_start_dirty;
    out_state->revision = runtime->editor_player_start_revision;
    out_state->saved_revision = runtime->editor_player_start_saved_revision;
    out_state->count = runtime->editor_player_start_count;
    return true;
}

static bool ensure_editor_player_start_capacity(slayer3d_game_data_runtime *runtime, int required_capacity)
{
    if (runtime == NULL || required_capacity <= runtime->editor_player_start_capacity)
        return runtime != NULL;
    int capacity = runtime->editor_player_start_capacity > 0 ? runtime->editor_player_start_capacity : 4;
    while (capacity < required_capacity)
        capacity *= 2;
    editor_player_start_runtime *starts =
        (editor_player_start_runtime *)SDL_realloc(runtime->editor_player_starts, (size_t)capacity * sizeof(*starts));
    if (starts == NULL)
        return false;
    SDL_memset(starts + runtime->editor_player_start_capacity, 0,
               (size_t)(capacity - runtime->editor_player_start_capacity) * sizeof(*starts));
    runtime->editor_player_starts = starts;
    runtime->editor_player_start_capacity = capacity;
    return true;
}

static bool duplicate_optional_string(const char *value, char **out_copy)
{
    if (out_copy != NULL)
        *out_copy = NULL;
    if (out_copy == NULL)
        return false;
    if (value == NULL || value[0] == '\0')
        return true;
    *out_copy = SDL_strdup(value);
    return *out_copy != NULL;
}

static void mark_editor_player_starts_dirty(slayer3d_game_data_runtime *runtime)
{
    runtime->editor_player_start_revision++;
    runtime->editor_player_start_dirty = true;
}

static bool editor_player_starts_set_source_path(slayer3d_game_data_runtime *runtime, const char *source_path)
{
    if (runtime == NULL || source_path == NULL)
        return true;
    char *copy = SDL_strdup(source_path);
    if (copy == NULL)
        return false;
    SDL_free(runtime->editor_player_start_source_path);
    runtime->editor_player_start_source_path = copy;
    return true;
}

bool slayer3d_game_data_mark_player_starts_saved(slayer3d_game_data_runtime *runtime, const char *source_path,
                                                 char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "player start save state requires runtime");
        return false;
    }
    if (!editor_player_starts_set_source_path(runtime, source_path))
    {
        set_error(error_buffer, error_buffer_size, "failed to update player start save state");
        return false;
    }
    runtime->editor_player_start_saved_revision = runtime->editor_player_start_revision;
    runtime->editor_player_start_dirty = false;
    return true;
}

bool slayer3d_game_data_place_editor_player_start(slayer3d_game_data_runtime *runtime,
                                                  const slayer3d_game_data_place_player_start_desc *desc,
                                                  char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL || desc == NULL || desc->name == NULL || desc->name[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "player start placement requires a runtime and name");
        return false;
    }

    const char *scene = first_non_empty_string(desc->scene, slayer3d_game_data_active_scene(runtime), NULL);
    if (!runtime_scene_exists(runtime, scene))
    {
        set_errorf(error_buffer, error_buffer_size, "player start scene '%s' not found", scene);
        return false;
    }

    slayer3d_registered_actor *target =
        desc->target != NULL && desc->target[0] != '\0' ? slayer3d_game_data_find_actor(runtime, desc->target) : NULL;
    if (desc->target != NULL && desc->target[0] != '\0' && target == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "player start target '%s' not found", desc->target);
        return false;
    }

    slayer3d_vec3 position = desc->position;
    if (!desc->has_position)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.hit)
            position = selection.point;
        else if (target != NULL)
            position = target->position;
        else
        {
            set_error(error_buffer, error_buffer_size, "player start placement requires a position or target");
            return false;
        }
    }

    const float yaw =
        desc->has_yaw || target == NULL ? desc->yaw : slayer3d_properties_get_float(target->props, "yaw", 0.0f);
    const float pitch =
        desc->has_pitch || target == NULL ? desc->pitch : slayer3d_properties_get_float(target->props, "pitch", 0.0f);

    char *scene_copy = NULL;
    char *target_copy = NULL;
    if (!duplicate_optional_string(scene, &scene_copy) || !duplicate_optional_string(desc->target, &target_copy))
    {
        SDL_free(scene_copy);
        SDL_free(target_copy);
        set_error(error_buffer, error_buffer_size, "failed to allocate player start fields");
        return false;
    }

    editor_player_start_runtime *entry = find_editor_player_start_mutable(runtime, desc->name);
    if (entry == NULL)
    {
        if (!ensure_editor_player_start_capacity(runtime, runtime->editor_player_start_count + 1))
        {
            SDL_free(scene_copy);
            SDL_free(target_copy);
            set_error(error_buffer, error_buffer_size, "failed to allocate player start");
            return false;
        }
        entry = &runtime->editor_player_starts[runtime->editor_player_start_count];
        entry->name = SDL_strdup(desc->name);
        if (entry->name == NULL)
        {
            SDL_free(scene_copy);
            SDL_free(target_copy);
            set_error(error_buffer, error_buffer_size, "failed to allocate player start name");
            return false;
        }
        runtime->editor_player_start_count++;
    }

    SDL_free(entry->scene);
    SDL_free(entry->target);
    entry->scene = scene_copy;
    entry->target = target_copy;
    entry->position = position;
    entry->yaw = yaw;
    entry->pitch = pitch;
    mark_editor_player_starts_dirty(runtime);

    if (desc->apply_to_target && target != NULL)
    {
        actor_set_position(target, position);
        slayer3d_properties_set_float(target->props, "yaw", yaw);
        slayer3d_properties_set_float(target->props, "pitch", pitch);
    }
    return true;
}

bool slayer3d_game_data_apply_editor_player_start(slayer3d_game_data_runtime *runtime, const char *name,
                                                  char *error_buffer, int error_buffer_size)
{
    const editor_player_start_runtime *start = find_editor_player_start(runtime, name);
    if (runtime == NULL || name == NULL || name[0] == '\0' || start == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "player start '%s' not found", name);
        return false;
    }
    if (start->target == NULL || start->target[0] == '\0')
    {
        set_errorf(error_buffer, error_buffer_size, "player start '%s' has no target actor", name);
        return false;
    }

    slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, start->target);
    if (target == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "player start target '%s' not found", start->target);
        return false;
    }

    actor_set_position(target, start->position);
    slayer3d_properties_set_float(target->props, "yaw", start->yaw);
    slayer3d_properties_set_float(target->props, "pitch", start->pitch);
    return true;
}

static bool editor_test_run_add_arg(yyjson_mut_doc *doc, yyjson_mut_val *args, const char *value)
{
    yyjson_mut_val *arg = yyjson_mut_strcpy(doc, value != NULL ? value : "");
    return arg != NULL && yyjson_mut_arr_append(args, arg);
}

bool slayer3d_game_data_export_editor_test_run_manifest_json(const slayer3d_game_data_runtime *runtime,
                                                             const slayer3d_game_data_editor_test_run_desc *desc,
                                                             char **out_json, size_t *out_size, char *error_buffer,
                                                             int error_buffer_size)
{
    if (out_json != NULL)
        *out_json = NULL;
    if (out_size != NULL)
        *out_size = 0u;
    if (runtime == NULL || desc == NULL || out_json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editor test run manifest requires runtime, descriptor, and output");
        return false;
    }
    if (desc->data_asset_path == NULL || desc->data_asset_path[0] == '\0')
    {
        set_error(error_buffer, error_buffer_size, "editor test run manifest requires a data asset path");
        return false;
    }

    const char *scene = desc->scene != NULL && desc->scene[0] != '\0' ? desc->scene : NULL;
    const char *player_start_name =
        desc->player_start != NULL && desc->player_start[0] != '\0' ? desc->player_start : NULL;
    slayer3d_game_data_editor_player_start player_start;
    SDL_zero(player_start);
    if (player_start_name != NULL)
    {
        if (!slayer3d_game_data_get_editor_player_start(runtime, player_start_name, &player_start))
        {
            set_errorf(error_buffer, error_buffer_size, "player start '%s' not found", player_start_name);
            return false;
        }
        if (player_start.target == NULL || player_start.target[0] == '\0')
        {
            set_errorf(error_buffer, error_buffer_size, "player start '%s' has no target actor", player_start_name);
            return false;
        }
        if (scene != NULL && player_start.scene != NULL && player_start.scene[0] != '\0' &&
            SDL_strcmp(scene, player_start.scene) != 0)
        {
            set_error(error_buffer, error_buffer_size, "editor test run scene conflicts with player start scene");
            return false;
        }
        if (scene == NULL && player_start.scene != NULL && player_start.scene[0] != '\0')
            scene = player_start.scene;
    }
    if (scene == NULL && player_start_name == NULL)
    {
        set_error(error_buffer, error_buffer_size, "editor test run manifest requires a scene or player start");
        return false;
    }
    if (scene != NULL && find_scene_const(runtime, scene) == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "scene '%s' not found", scene);
        return false;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *args = doc != NULL ? yyjson_mut_arr(doc) : NULL;
    if (doc == NULL || root == NULL || args == NULL)
    {
        yyjson_mut_doc_free(doc);
        set_error(error_buffer, error_buffer_size, "failed to allocate editor test run manifest");
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    bool ok = yyjson_mut_obj_add_strcpy(doc, root, "schema", "slayer3d.editor_test_run.v0") &&
              yyjson_mut_obj_add_strcpy(doc, root, "data", desc->data_asset_path) &&
              yyjson_mut_obj_add_val(doc, root, "args", args) && editor_test_run_add_arg(doc, args, "--data") &&
              editor_test_run_add_arg(doc, args, desc->data_asset_path);
    if (ok && scene != NULL)
    {
        ok = yyjson_mut_obj_add_strcpy(doc, root, "scene", scene) && editor_test_run_add_arg(doc, args, "--scene") &&
             editor_test_run_add_arg(doc, args, scene);
    }
    if (ok && player_start_name != NULL)
    {
        ok = yyjson_mut_obj_add_strcpy(doc, root, "player_start", player_start_name) &&
             yyjson_mut_obj_add_strcpy(doc, root, "target", player_start.target) &&
             editor_test_run_add_arg(doc, args, "--player-start") &&
             editor_test_run_add_arg(doc, args, player_start_name);
    }

    size_t size = 0u;
    char *json = ok ? yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size) : NULL;
    yyjson_mut_doc_free(doc);
    if (json == NULL)
    {
        set_error(error_buffer, error_buffer_size, "failed to write editor test run manifest JSON");
        return false;
    }

    char *copy = (char *)SDL_malloc(size + 1u);
    if (copy == NULL)
    {
        free(json);
        set_error(error_buffer, error_buffer_size, "failed to allocate editor test run manifest JSON");
        return false;
    }
    SDL_memcpy(copy, json, size + 1u);
    free(json);
    *out_json = copy;
    if (out_size != NULL)
        *out_size = size;
    return true;
}

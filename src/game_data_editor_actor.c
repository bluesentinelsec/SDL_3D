/**
 * @file game_data_editor_actor.c
 * @brief Editor actor placement, state, and selection helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

static editor_actor_runtime *find_editor_actor_mutable(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
    {
        if (runtime->editor_actors[i].name != NULL && SDL_strcmp(runtime->editor_actors[i].name, name) == 0)
            return &runtime->editor_actors[i];
    }
    return NULL;
}

const editor_actor_runtime *find_editor_actor(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_editor_actor_mutable((slayer3d_game_data_runtime *)runtime, name);
}

static bool runtime_scene_exists(const slayer3d_game_data_runtime *runtime, const char *scene)
{
    return scene == NULL || scene[0] == '\0' || find_scene_const(runtime, scene) != NULL;
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

static bool ensure_editor_actor_capacity(slayer3d_game_data_runtime *runtime, int required_capacity)
{
    if (runtime == NULL || required_capacity <= runtime->editor_actor_capacity)
        return runtime != NULL;
    int capacity = runtime->editor_actor_capacity > 0 ? runtime->editor_actor_capacity : 8;
    while (capacity < required_capacity)
        capacity *= 2;
    editor_actor_runtime *actors =
        (editor_actor_runtime *)SDL_realloc(runtime->editor_actors, (size_t)capacity * sizeof(*actors));
    if (actors == NULL)
        return false;
    SDL_memset(actors + runtime->editor_actor_capacity, 0,
               (size_t)(capacity - runtime->editor_actor_capacity) * sizeof(*actors));
    runtime->editor_actors = actors;
    runtime->editor_actor_capacity = capacity;
    return true;
}

static bool editor_actor_name_exists(const slayer3d_game_data_runtime *runtime, const char *name)
{
    return find_editor_actor(runtime, name) != NULL;
}

static bool generate_editor_actor_name(const slayer3d_game_data_runtime *runtime, const char *prefix, char *buffer,
                                       size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    const char *base = prefix != NULL && prefix[0] != '\0' ? prefix : "actor.editor";
    for (int i = 1; i < 100000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.%d", base, i);
        if (!editor_actor_name_exists(runtime, buffer))
            return true;
    }
    buffer[0] = '\0';
    return false;
}

static bool copy_properties(const slayer3d_properties *source, slayer3d_properties **out_copy)
{
    if (out_copy != NULL)
        *out_copy = NULL;
    if (out_copy == NULL)
        return false;
    slayer3d_properties *copy = slayer3d_properties_create();
    if (copy == NULL)
        return false;
    const int count = source != NULL ? slayer3d_properties_count(source) : 0;
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        slayer3d_value_type type = SLAYER3D_VALUE_STRING;
        if (!slayer3d_properties_get_key_at(source, i, &key, &type))
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(source, key);
        if (key != NULL && value != NULL && !set_property_from_value(copy, key, value))
        {
            slayer3d_properties_destroy(copy);
            return false;
        }
    }
    *out_copy = copy;
    return true;
}

static bool copy_properties_from_json(yyjson_val *properties, slayer3d_properties **out_copy)
{
    if (out_copy != NULL)
        *out_copy = NULL;
    if (properties == NULL)
        return copy_properties(NULL, out_copy);
    if (!yyjson_is_obj(properties))
        return false;
    slayer3d_properties *copy = slayer3d_properties_create();
    if (copy == NULL)
        return false;

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name == NULL || name[0] == '\0' || !set_property_from_json(copy, name, value))
        {
            slayer3d_properties_destroy(copy);
            return false;
        }
    }
    *out_copy = copy;
    return true;
}

static void free_editor_actor_entry(editor_actor_runtime *actor)
{
    if (actor == NULL)
        return;
    SDL_free(actor->name);
    SDL_free(actor->scene);
    SDL_free(actor->display_name);
    SDL_free(actor->archetype);
    SDL_free(actor->mesh);
    SDL_free(actor->model);
    SDL_free(actor->group);
    slayer3d_properties_destroy(actor->properties);
    SDL_zero(*actor);
}

void free_editor_actors_runtime(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->editor_actor_count; ++i)
        free_editor_actor_entry(&runtime->editor_actors[i]);
    SDL_free(runtime->editor_actors);
    runtime->editor_actors = NULL;
    runtime->editor_actor_count = 0;
    runtime->editor_actor_capacity = 0;
    runtime->editor_actor_revision = 0;
    runtime->editor_actor_dirty = false;
}

static void mark_editor_actors_dirty(slayer3d_game_data_runtime *runtime)
{
    runtime->editor_actor_revision++;
    runtime->editor_actor_dirty = true;
}

bool slayer3d_game_data_get_editor_actor(const slayer3d_game_data_runtime *runtime, const char *name,
                                         slayer3d_game_data_editor_actor *out_actor)
{
    if (out_actor != NULL)
        SDL_zero(*out_actor);
    const editor_actor_runtime *actor = find_editor_actor(runtime, name);
    if (actor == NULL || out_actor == NULL)
        return false;
    out_actor->name = actor->name;
    out_actor->scene = actor->scene;
    out_actor->display_name = actor->display_name;
    out_actor->archetype = actor->archetype;
    out_actor->mesh = actor->mesh;
    out_actor->model = actor->model;
    out_actor->group = actor->group;
    out_actor->position = actor->position;
    out_actor->rotation = actor->rotation;
    out_actor->scale = actor->scale;
    out_actor->color = actor->color;
    out_actor->properties = actor->properties;
    return true;
}

bool slayer3d_game_data_get_editor_actor_state(const slayer3d_game_data_runtime *runtime,
                                               slayer3d_game_data_editor_actor_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    if (runtime == NULL || out_state == NULL)
        return false;
    out_state->dirty = runtime->editor_actor_dirty;
    out_state->revision = runtime->editor_actor_revision;
    out_state->count = runtime->editor_actor_count;
    return true;
}

static bool assign_editor_actor_strings(editor_actor_runtime *entry,
                                        const slayer3d_game_data_place_editor_actor_desc *desc, const char *scene,
                                        const char *name, char *error_buffer, int error_buffer_size)
{
    char *name_copy = NULL;
    char *scene_copy = NULL;
    char *display_name_copy = NULL;
    char *archetype_copy = NULL;
    char *mesh_copy = NULL;
    char *model_copy = NULL;
    char *group_copy = NULL;
    if (!duplicate_optional_string(name, &name_copy) || !duplicate_optional_string(scene, &scene_copy) ||
        !duplicate_optional_string(desc->display_name, &display_name_copy) ||
        !duplicate_optional_string(desc->archetype, &archetype_copy) ||
        !duplicate_optional_string(desc->mesh, &mesh_copy) || !duplicate_optional_string(desc->model, &model_copy) ||
        !duplicate_optional_string(desc->group, &group_copy))
    {
        SDL_free(name_copy);
        SDL_free(scene_copy);
        SDL_free(display_name_copy);
        SDL_free(archetype_copy);
        SDL_free(mesh_copy);
        SDL_free(model_copy);
        SDL_free(group_copy);
        set_error(error_buffer, error_buffer_size, "failed to allocate editor actor fields");
        return false;
    }

    SDL_free(entry->name);
    SDL_free(entry->scene);
    SDL_free(entry->display_name);
    SDL_free(entry->archetype);
    SDL_free(entry->mesh);
    SDL_free(entry->model);
    SDL_free(entry->group);
    entry->name = name_copy;
    entry->scene = scene_copy;
    entry->display_name = display_name_copy;
    entry->archetype = archetype_copy;
    entry->mesh = mesh_copy;
    entry->model = model_copy;
    entry->group = group_copy;
    return true;
}

bool slayer3d_game_data_place_editor_actor(slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_place_editor_actor_desc *desc, char *out_name,
                                           size_t out_name_size, char *error_buffer, int error_buffer_size)
{
    if (out_name != NULL && out_name_size > 0U)
        out_name[0] = '\0';
    if (runtime == NULL || desc == NULL)
    {
        set_error(error_buffer, error_buffer_size, "actor placement requires a runtime and descriptor");
        return false;
    }

    char generated_name[128];
    const char *name = desc->name;
    if (name == NULL || name[0] == '\0')
    {
        if (!generate_editor_actor_name(runtime, desc->name_prefix, generated_name, sizeof(generated_name)))
        {
            set_error(error_buffer, error_buffer_size, "failed to generate editor actor name");
            return false;
        }
        name = generated_name;
    }

    const char *scene = first_non_empty_string(desc->scene, slayer3d_game_data_active_scene(runtime), NULL);
    if (!runtime_scene_exists(runtime, scene))
    {
        set_errorf(error_buffer, error_buffer_size, "actor scene '%s' not found", scene);
        return false;
    }

    slayer3d_vec3 position = desc->position;
    if (!desc->has_position)
    {
        if (editor_placement_preview_active_for_scene(runtime) && runtime->editor_placement_preview.active)
            position = runtime->editor_placement_preview.anchor;
        else
        {
            slayer3d_game_data_editor_selection selection;
            SDL_zero(selection);
            if (slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.hit)
                position = selection.point;
            else
            {
                set_error(error_buffer, error_buffer_size, "actor placement requires a position or hovered preview");
                return false;
            }
        }
    }

    slayer3d_properties *properties = NULL;
    if (!copy_properties(desc->properties, &properties))
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate actor properties");
        return false;
    }

    editor_actor_runtime *entry = find_editor_actor_mutable(runtime, name);
    if (entry == NULL)
    {
        if (!ensure_editor_actor_capacity(runtime, runtime->editor_actor_count + 1))
        {
            slayer3d_properties_destroy(properties);
            set_error(error_buffer, error_buffer_size, "failed to allocate editor actor");
            return false;
        }
        entry = &runtime->editor_actors[runtime->editor_actor_count++];
    }

    if (!assign_editor_actor_strings(entry, desc, scene, name, error_buffer, error_buffer_size))
    {
        slayer3d_properties_destroy(properties);
        return false;
    }
    slayer3d_properties_destroy(entry->properties);
    entry->properties = properties;
    entry->position = position;
    entry->rotation = desc->has_rotation ? desc->rotation : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    entry->scale = desc->has_scale ? desc->scale : slayer3d_vec3_make(1.0f, 1.0f, 1.0f);
    entry->color = desc->has_color ? desc->color : (slayer3d_color){120, 200, 255, 210};
    mark_editor_actors_dirty(runtime);
    if (out_name != NULL && out_name_size > 0U)
        SDL_strlcpy(out_name, name, out_name_size);
    return true;
}

bool load_editor_actors(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                        int error_buffer_size)
{
    yyjson_val *actors = obj_get(root, "editor_actors");
    if (actors == NULL)
        return true;
    if (runtime == NULL || !yyjson_is_arr(actors))
    {
        set_error(error_buffer, error_buffer_size, "editor_actors must be an array");
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(actors); ++i)
    {
        yyjson_val *json = yyjson_arr_get(actors, i);
        if (!yyjson_is_obj(json))
        {
            set_error(error_buffer, error_buffer_size, "editor actor entry must be an object");
            return false;
        }
        slayer3d_properties *properties = NULL;
        if (!copy_properties_from_json(obj_get(json, "properties"), &properties))
        {
            set_error(error_buffer, error_buffer_size, "editor actor properties must be an object");
            return false;
        }
        slayer3d_game_data_place_editor_actor_desc desc;
        SDL_zero(desc);
        desc.name = json_string(json, "name", NULL);
        desc.scene = json_string(json, "scene", NULL);
        desc.display_name = json_string(json, "display_name", NULL);
        desc.archetype = json_string(json, "archetype", NULL);
        desc.mesh = json_string(json, "mesh", "box");
        desc.model = json_string(json, "model", NULL);
        desc.group = json_string(json, "group", NULL);
        desc.position = json_vec3(json, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        desc.has_position = obj_get(json, "position") != NULL;
        desc.rotation = json_vec3(json, "rotation", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        desc.has_rotation = obj_get(json, "rotation") != NULL;
        desc.scale = json_vec3(json, "scale", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
        desc.has_scale = obj_get(json, "scale") != NULL;
        desc.color = json_color(json, "color", (slayer3d_color){120, 200, 255, 210});
        desc.has_color = obj_get(json, "color") != NULL;
        desc.properties = properties;
        char name[128];
        const bool ok =
            slayer3d_game_data_place_editor_actor(runtime, &desc, name, sizeof(name), error_buffer, error_buffer_size);
        slayer3d_properties_destroy(properties);
        if (!ok)
            return false;
    }
    runtime->editor_actor_dirty = false;
    runtime->editor_actor_revision = 0;
    return true;
}

static void publish_editor_actor_outputs(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool ok,
                                         const char *message, const char *name)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "actor_key", ok ? name : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key", runtime != NULL ? runtime->editor_actor_dirty : false);
    editor_set_int_output(scene_state, outputs, "revision_key",
                          runtime != NULL ? (int)runtime->editor_actor_revision : 0);
    editor_set_int_output(scene_state, outputs, "count_key", runtime != NULL ? runtime->editor_actor_count : 0);
}

bool slayer3d_game_data_place_editor_actor_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_properties *properties = properties_from_json_payload(obj_get(action, "properties"), NULL);
    if (properties == NULL)
    {
        publish_editor_actor_outputs(runtime, outputs, false, "actor properties must be an object", "");
        return true;
    }

    slayer3d_game_data_place_editor_actor_desc desc;
    SDL_zero(desc);
    desc.name = json_string(action, "name", NULL);
    desc.name_prefix = json_string(action, "name_prefix", "actor.editor_shell");
    desc.scene = json_string(action, "scene", NULL);
    desc.display_name = json_string(action, "display_name", NULL);
    desc.archetype = json_string(action, "archetype", NULL);
    desc.mesh = json_string(action, "mesh", "box");
    desc.model = json_string(action, "model", NULL);
    desc.group = json_string(action, "group", NULL);
    const char *position_from = json_string(action, "position_from", NULL);
    if (position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0)
    {
        desc.position =
            runtime != NULL ? runtime->editor_placement_preview.anchor : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        desc.has_position = runtime != NULL && editor_placement_preview_active_for_scene(runtime);
    }
    else if (position_from != NULL && SDL_strcmp(position_from, "selection_point") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (slayer3d_game_data_get_active_editor_selection(runtime, &selection) && selection.hit)
        {
            desc.position = selection.point;
            desc.has_position = true;
        }
    }
    else
    {
        desc.position = json_vec3(action, "position", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        desc.has_position = obj_get(action, "position") != NULL;
    }
    desc.rotation = json_vec3(action, "rotation", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.has_rotation = obj_get(action, "rotation") != NULL;
    desc.scale = json_vec3(action, "scale", slayer3d_vec3_make(1.0f, 1.0f, 1.0f));
    desc.has_scale = obj_get(action, "scale") != NULL;
    desc.color = json_color(action, "color", (slayer3d_color){120, 200, 255, 210});
    desc.has_color = obj_get(action, "color") != NULL;
    desc.properties = properties;

    char actor_name[128];
    char error[192];
    const bool ok =
        slayer3d_game_data_place_editor_actor(runtime, &desc, actor_name, sizeof(actor_name), error, sizeof(error));
    slayer3d_properties_destroy(properties);
    publish_editor_actor_outputs(runtime, outputs, ok, ok ? json_string(action, "message", "actor placed") : error,
                                 ok ? actor_name : "");
    return true;
}

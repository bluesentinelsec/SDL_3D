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

static const char *editor_property_type_name(slayer3d_value_type type)
{
    switch (type)
    {
    case SLAYER3D_VALUE_INT:
        return "int";
    case SLAYER3D_VALUE_FLOAT:
        return "float";
    case SLAYER3D_VALUE_BOOL:
        return "bool";
    case SLAYER3D_VALUE_VEC3:
        return "vec3";
    case SLAYER3D_VALUE_STRING:
        return "string";
    case SLAYER3D_VALUE_COLOR:
        return "color";
    }
    return "unknown";
}

static void editor_property_value_to_string(const slayer3d_value *value, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return;
    buffer[0] = '\0';
    if (value == NULL)
        return;
    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        SDL_snprintf(buffer, buffer_size, "%d", value->as_int);
        break;
    case SLAYER3D_VALUE_FLOAT:
        SDL_snprintf(buffer, buffer_size, "%.3f", value->as_float);
        break;
    case SLAYER3D_VALUE_BOOL:
        SDL_snprintf(buffer, buffer_size, "%s", value->as_bool ? "true" : "false");
        break;
    case SLAYER3D_VALUE_VEC3:
        SDL_snprintf(buffer, buffer_size, "%.3f, %.3f, %.3f", value->as_vec3.x, value->as_vec3.y, value->as_vec3.z);
        break;
    case SLAYER3D_VALUE_STRING:
        SDL_snprintf(buffer, buffer_size, "%s", value->as_string != NULL ? value->as_string : "");
        break;
    case SLAYER3D_VALUE_COLOR:
        SDL_snprintf(buffer, buffer_size, "%u, %u, %u, %u", value->as_color.r, value->as_color.g, value->as_color.b,
                     value->as_color.a);
        break;
    }
}

static void clear_editor_property_slots(slayer3d_properties *scene_state, int slot_count)
{
    if (scene_state == NULL)
        return;
    for (int i = 0; i < slot_count; ++i)
    {
        char key[96];
        SDL_snprintf(key, sizeof(key), "editor.property.slot.%d.available", i);
        slayer3d_properties_set_bool(scene_state, key, false);
        SDL_snprintf(key, sizeof(key), "editor.property.slot.%d.key", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.property.slot.%d.type", i);
        slayer3d_properties_set_string(scene_state, key, "");
        SDL_snprintf(key, sizeof(key), "editor.property.slot.%d.value", i);
        slayer3d_properties_set_string(scene_state, key, "");
    }
}

void publish_editor_selection_properties(slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_editor_selection *selection, int slot_count)
{
    if (runtime == NULL || runtime->scene_state == NULL || slot_count <= 0)
        return;

    slayer3d_properties *scene_state = runtime->scene_state;
    const bool has_actor = selection != NULL && selection->hit &&
                           selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR &&
                           selection->element_name != NULL && selection->element_name[0] != '\0';
    const editor_actor_runtime *actor = has_actor ? find_editor_actor(runtime, selection->element_name) : NULL;
    const int property_count =
        actor != NULL && actor->properties != NULL ? slayer3d_properties_count(actor->properties) : 0;
    slayer3d_properties_set_string(scene_state, "editor.property.target.type", actor != NULL ? "editor_actor" : "none");
    slayer3d_properties_set_string(scene_state, "editor.property.target.name", actor != NULL ? actor->name : "");
    slayer3d_properties_set_int(scene_state, "editor.property.count", property_count);
    clear_editor_property_slots(scene_state, slot_count);

    for (int i = 0; i < property_count && i < slot_count; ++i)
    {
        const char *key_name = NULL;
        slayer3d_value_type type = SLAYER3D_VALUE_STRING;
        if (!slayer3d_properties_get_key_at(actor->properties, i, &key_name, &type) || key_name == NULL)
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(actor->properties, key_name);
        char slot_key[96];
        char formatted[256];
        editor_property_value_to_string(value, formatted, sizeof(formatted));
        SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.available", i);
        slayer3d_properties_set_bool(scene_state, slot_key, true);
        SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.key", i);
        slayer3d_properties_set_string(scene_state, slot_key, key_name);
        SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.type", i);
        slayer3d_properties_set_string(scene_state, slot_key, editor_property_type_name(type));
        SDL_snprintf(slot_key, sizeof(slot_key), "editor.property.slot.%d.value", i);
        slayer3d_properties_set_string(scene_state, slot_key, formatted);
    }
}

static const char *editor_property_action_string_from_state(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                            const char *literal_key, const char *state_key,
                                                            const char *fallback)
{
    const char *state_name = json_string(action, state_key, NULL);
    if (state_name != NULL && runtime != NULL && runtime->scene_state != NULL)
        return slayer3d_properties_get_string(runtime->scene_state, state_name, fallback);
    return json_string(action, literal_key, fallback);
}

static const char *editor_property_action_target_name(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      char *error, size_t error_size)
{
    const char *target_type = json_string(action, "target_type", "selection");
    if (SDL_strcmp(target_type, "selection") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (!slayer3d_game_data_get_active_editor_selection(runtime, &selection) || !selection.hit ||
            selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR || selection.element_name == NULL ||
            selection.element_name[0] == '\0')
        {
            SDL_snprintf(error, error_size, "select an editor actor before editing properties");
            return NULL;
        }
        return selection.element_name;
    }
    if (SDL_strcmp(target_type, "editor_actor") == 0)
    {
        const char *target =
            editor_property_action_string_from_state(runtime, action, "target", "target_from_state", NULL);
        if (target == NULL || target[0] == '\0')
        {
            SDL_snprintf(error, error_size, "editor property action requires an actor target");
            return NULL;
        }
        return target;
    }
    SDL_snprintf(error, error_size, "editor property target type '%s' is unsupported", target_type);
    return NULL;
}

static bool editor_property_action_value(slayer3d_game_data_runtime *runtime, editor_actor_runtime *actor,
                                         const char *key, yyjson_val *action, const slayer3d_properties *payload)
{
    const char *value_from_state = json_string(action, "value_from_state", NULL);
    if (value_from_state != NULL)
    {
        const slayer3d_value *value = runtime != NULL && runtime->scene_state != NULL
                                          ? slayer3d_properties_get_value(runtime->scene_state, value_from_state)
                                          : NULL;
        return value != NULL && set_property_from_value(actor->properties, key, value);
    }
    const char *value_from_payload = json_string(action, "value_from_payload", NULL);
    if (value_from_payload != NULL)
    {
        const slayer3d_value *value =
            payload != NULL ? slayer3d_properties_get_value(payload, value_from_payload) : NULL;
        return value != NULL && set_property_from_value(actor->properties, key, value);
    }
    return set_property_from_json_with_payload(actor->properties, key, obj_get(action, "value"), payload);
}

static void publish_editor_property_action_outputs(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool ok,
                                                   const char *message, const char *target, const char *key)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "target_key", ok && target != NULL ? target : "");
    editor_set_string_output(scene_state, outputs, "key_key", ok && key != NULL ? key : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key", runtime != NULL ? runtime->editor_actor_dirty : false);
    editor_set_int_output(scene_state, outputs, "revision_key",
                          runtime != NULL ? (int)runtime->editor_actor_revision : 0);
    editor_set_int_output(scene_state, outputs, "count_key", runtime != NULL ? runtime->editor_actor_count : 0);
}

static void refresh_editor_property_selection_if_needed(slayer3d_game_data_runtime *runtime, const char *target)
{
    slayer3d_game_data_editor_selection selection;
    SDL_zero(selection);
    if (runtime != NULL && target != NULL && slayer3d_game_data_get_active_editor_selection(runtime, &selection) &&
        selection.hit && selection.type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR &&
        selection.element_name != NULL && SDL_strcmp(selection.element_name, target) == 0)
    {
        publish_editor_selection_properties(runtime, &selection, 6);
    }
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

static editor_connection_runtime *find_editor_connection_mutable(slayer3d_game_data_runtime *runtime, const char *id)
{
    if (runtime == NULL || id == NULL)
        return NULL;
    for (int i = 0; i < runtime->editor_connection_count; ++i)
    {
        if (runtime->editor_connections[i].id != NULL && SDL_strcmp(runtime->editor_connections[i].id, id) == 0)
            return &runtime->editor_connections[i];
    }
    return NULL;
}

static const editor_connection_runtime *find_editor_connection(const slayer3d_game_data_runtime *runtime,
                                                               const char *id)
{
    return find_editor_connection_mutable((slayer3d_game_data_runtime *)runtime, id);
}

static bool ensure_editor_connection_capacity(slayer3d_game_data_runtime *runtime, int required_capacity)
{
    if (runtime == NULL || required_capacity <= runtime->editor_connection_capacity)
        return runtime != NULL;
    int capacity = runtime->editor_connection_capacity > 0 ? runtime->editor_connection_capacity : 8;
    while (capacity < required_capacity)
        capacity *= 2;
    editor_connection_runtime *connections =
        (editor_connection_runtime *)SDL_realloc(runtime->editor_connections, (size_t)capacity * sizeof(*connections));
    if (connections == NULL)
        return false;
    SDL_memset(connections + runtime->editor_connection_capacity, 0,
               (size_t)(capacity - runtime->editor_connection_capacity) * sizeof(*connections));
    runtime->editor_connections = connections;
    runtime->editor_connection_capacity = capacity;
    return true;
}

static bool editor_connection_id_exists(const slayer3d_game_data_runtime *runtime, const char *id)
{
    return find_editor_connection(runtime, id) != NULL;
}

static bool generate_editor_connection_id(const slayer3d_game_data_runtime *runtime, const char *prefix, char *buffer,
                                          size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
        return false;
    const char *base = prefix != NULL && prefix[0] != '\0' ? prefix : "connection.editor";
    for (int i = 1; i < 100000; ++i)
    {
        SDL_snprintf(buffer, buffer_size, "%s.%d", base, i);
        if (!editor_connection_id_exists(runtime, buffer))
            return true;
    }
    buffer[0] = '\0';
    return false;
}

static void free_editor_connection_endpoint(editor_connection_endpoint_runtime *endpoint)
{
    if (endpoint == NULL)
        return;
    SDL_free(endpoint->entity);
    SDL_free(endpoint->event);
    SDL_free(endpoint->action);
    SDL_zero(*endpoint);
}

static void free_editor_connection_entry(editor_connection_runtime *connection)
{
    if (connection == NULL)
        return;
    SDL_free(connection->id);
    free_editor_connection_endpoint(&connection->from);
    free_editor_connection_endpoint(&connection->to);
    slayer3d_properties_destroy(connection->properties);
    SDL_zero(*connection);
}

void free_editor_connections_runtime(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->editor_connection_count; ++i)
        free_editor_connection_entry(&runtime->editor_connections[i]);
    SDL_free(runtime->editor_connections);
    runtime->editor_connections = NULL;
    runtime->editor_connection_count = 0;
    runtime->editor_connection_capacity = 0;
    runtime->editor_connection_revision = 0;
    runtime->editor_connection_dirty = false;
}

static void mark_editor_connections_dirty(slayer3d_game_data_runtime *runtime)
{
    runtime->editor_connection_revision++;
    runtime->editor_connection_dirty = true;
}

static bool copy_editor_connection_endpoint(const slayer3d_game_data_editor_connection_endpoint *source,
                                            editor_connection_endpoint_runtime *dest)
{
    if (source == NULL || dest == NULL)
        return false;
    editor_connection_endpoint_runtime copy;
    SDL_zero(copy);
    copy.external = source->external;
    if (!duplicate_optional_string(source->entity, &copy.entity) ||
        !duplicate_optional_string(source->event, &copy.event) ||
        !duplicate_optional_string(source->action, &copy.action))
    {
        free_editor_connection_endpoint(&copy);
        return false;
    }
    free_editor_connection_endpoint(dest);
    *dest = copy;
    return true;
}

static bool editor_connection_endpoint_valid(const slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_editor_connection_endpoint *endpoint,
                                             const char *label, char *error_buffer, int error_buffer_size)
{
    if (endpoint == NULL || endpoint->entity == NULL || endpoint->entity[0] == '\0')
    {
        set_errorf(error_buffer, error_buffer_size, "connection %s endpoint requires an entity", label);
        return false;
    }
    if (!endpoint->external && find_editor_actor(runtime, endpoint->entity) == NULL)
    {
        set_errorf(error_buffer, error_buffer_size, "connection %s actor '%s' not found", label, endpoint->entity);
        return false;
    }
    return true;
}

bool slayer3d_game_data_get_editor_connection(const slayer3d_game_data_runtime *runtime, const char *id,
                                              slayer3d_game_data_editor_connection *out_connection)
{
    if (out_connection != NULL)
        SDL_zero(*out_connection);
    const editor_connection_runtime *connection = find_editor_connection(runtime, id);
    if (connection == NULL || out_connection == NULL)
        return false;
    out_connection->id = connection->id;
    out_connection->from.entity = connection->from.entity;
    out_connection->from.event = connection->from.event;
    out_connection->from.action = connection->from.action;
    out_connection->from.external = connection->from.external;
    out_connection->to.entity = connection->to.entity;
    out_connection->to.event = connection->to.event;
    out_connection->to.action = connection->to.action;
    out_connection->to.external = connection->to.external;
    out_connection->properties = connection->properties;
    return true;
}

bool slayer3d_game_data_get_editor_connection_state(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_connection_state *out_state)
{
    if (out_state != NULL)
        SDL_zero(*out_state);
    if (runtime == NULL || out_state == NULL)
        return false;
    out_state->dirty = runtime->editor_connection_dirty;
    out_state->revision = runtime->editor_connection_revision;
    out_state->count = runtime->editor_connection_count;
    return true;
}

bool slayer3d_game_data_place_editor_connection(slayer3d_game_data_runtime *runtime,
                                                const slayer3d_game_data_place_editor_connection_desc *desc,
                                                char *out_id, size_t out_id_size, char *error_buffer,
                                                int error_buffer_size)
{
    if (out_id != NULL && out_id_size > 0U)
        out_id[0] = '\0';
    if (runtime == NULL || desc == NULL)
    {
        set_error(error_buffer, error_buffer_size, "connection placement requires a runtime and descriptor");
        return false;
    }
    if (!editor_connection_endpoint_valid(runtime, &desc->from, "source", error_buffer, error_buffer_size) ||
        !editor_connection_endpoint_valid(runtime, &desc->to, "target", error_buffer, error_buffer_size))
    {
        return false;
    }

    char generated_id[160];
    const char *id = desc->id;
    if (id == NULL || id[0] == '\0')
    {
        if (!generate_editor_connection_id(runtime, desc->id_prefix, generated_id, sizeof(generated_id)))
        {
            set_error(error_buffer, error_buffer_size, "failed to generate editor connection id");
            return false;
        }
        id = generated_id;
    }

    slayer3d_properties *properties = NULL;
    if (!copy_properties(desc->properties, &properties))
    {
        set_error(error_buffer, error_buffer_size, "failed to allocate connection properties");
        return false;
    }

    char *id_copy = NULL;
    if (!duplicate_optional_string(id, &id_copy))
    {
        slayer3d_properties_destroy(properties);
        set_error(error_buffer, error_buffer_size, "failed to allocate connection id");
        return false;
    }

    editor_connection_endpoint_runtime from_copy;
    editor_connection_endpoint_runtime to_copy;
    SDL_zero(from_copy);
    SDL_zero(to_copy);
    if (!copy_editor_connection_endpoint(&desc->from, &from_copy) ||
        !copy_editor_connection_endpoint(&desc->to, &to_copy))
    {
        SDL_free(id_copy);
        slayer3d_properties_destroy(properties);
        free_editor_connection_endpoint(&from_copy);
        free_editor_connection_endpoint(&to_copy);
        set_error(error_buffer, error_buffer_size, "failed to allocate connection endpoints");
        return false;
    }

    editor_connection_runtime *entry = find_editor_connection_mutable(runtime, id);
    if (entry == NULL)
    {
        if (!ensure_editor_connection_capacity(runtime, runtime->editor_connection_count + 1))
        {
            SDL_free(id_copy);
            slayer3d_properties_destroy(properties);
            free_editor_connection_endpoint(&from_copy);
            free_editor_connection_endpoint(&to_copy);
            set_error(error_buffer, error_buffer_size, "failed to allocate editor connection");
            return false;
        }
        entry = &runtime->editor_connections[runtime->editor_connection_count++];
    }

    SDL_free(entry->id);
    free_editor_connection_endpoint(&entry->from);
    free_editor_connection_endpoint(&entry->to);
    slayer3d_properties_destroy(entry->properties);
    entry->id = id_copy;
    entry->from = from_copy;
    entry->to = to_copy;
    entry->properties = properties;
    mark_editor_connections_dirty(runtime);
    if (out_id != NULL && out_id_size > 0U)
        SDL_strlcpy(out_id, id, out_id_size);
    return true;
}

static slayer3d_game_data_editor_connection_endpoint editor_connection_endpoint_from_json(yyjson_val *json,
                                                                                          const char *event_fallback,
                                                                                          const char *action_fallback)
{
    slayer3d_game_data_editor_connection_endpoint endpoint;
    SDL_zero(endpoint);
    endpoint.entity = json_string(json, "entity", NULL);
    endpoint.event = json_string(json, "event", event_fallback);
    endpoint.action = json_string(json, "action", action_fallback);
    endpoint.external = json_bool(json, "external", false);
    return endpoint;
}

bool load_editor_connections(slayer3d_game_data_runtime *runtime, yyjson_val *root, char *error_buffer,
                             int error_buffer_size)
{
    yyjson_val *connections = obj_get(root, "editor_connections");
    if (connections == NULL)
        return true;
    if (runtime == NULL || !yyjson_is_arr(connections))
    {
        set_error(error_buffer, error_buffer_size, "editor_connections must be an array");
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(connections); ++i)
    {
        yyjson_val *json = yyjson_arr_get(connections, i);
        if (!yyjson_is_obj(json))
        {
            set_error(error_buffer, error_buffer_size, "editor connection entry must be an object");
            return false;
        }
        yyjson_val *from = obj_get(json, "from");
        yyjson_val *to = obj_get(json, "to");
        if (!yyjson_is_obj(from) || !yyjson_is_obj(to))
        {
            set_error(error_buffer, error_buffer_size, "editor connection requires from and to endpoints");
            return false;
        }
        slayer3d_properties *properties = NULL;
        if (!copy_properties_from_json(obj_get(json, "properties"), &properties))
        {
            set_error(error_buffer, error_buffer_size, "editor connection properties must be an object");
            return false;
        }
        slayer3d_game_data_place_editor_connection_desc desc;
        SDL_zero(desc);
        desc.id = json_string(json, "id", NULL);
        desc.id_prefix = json_string(json, "id_prefix", "connection.editor");
        desc.from = editor_connection_endpoint_from_json(from, "activate", NULL);
        desc.to = editor_connection_endpoint_from_json(to, NULL, "trigger");
        desc.properties = properties;
        char id[160];
        const bool ok =
            slayer3d_game_data_place_editor_connection(runtime, &desc, id, sizeof(id), error_buffer, error_buffer_size);
        slayer3d_properties_destroy(properties);
        if (!ok)
            return false;
    }
    runtime->editor_connection_dirty = false;
    runtime->editor_connection_revision = 0;
    return true;
}

static const char *editor_connection_selected_actor(slayer3d_game_data_runtime *runtime, char *error, size_t error_size)
{
    slayer3d_game_data_editor_selection selection;
    SDL_zero(selection);
    if (!slayer3d_game_data_get_active_editor_selection(runtime, &selection) || !selection.hit ||
        selection.type != SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_ACTOR || selection.element_name == NULL ||
        selection.element_name[0] == '\0')
    {
        SDL_snprintf(error, error_size, "select an editor actor before connecting");
        return NULL;
    }
    return selection.element_name;
}

static const char *editor_connection_string_from_state(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                       const char *literal_key, const char *state_key,
                                                       const char *fallback)
{
    const char *state_name = json_string(action, state_key, NULL);
    if (state_name != NULL && runtime != NULL && runtime->scene_state != NULL)
        return slayer3d_properties_get_string(runtime->scene_state, state_name, fallback);
    return json_string(action, literal_key, fallback);
}

static void publish_editor_connection_outputs(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool ok,
                                              const char *message, const char *id, const char *source,
                                              const char *target)
{
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "connection_key", ok && id != NULL ? id : "");
    editor_set_string_output(scene_state, outputs, "source_key", ok && source != NULL ? source : "");
    editor_set_string_output(scene_state, outputs, "target_key", ok && target != NULL ? target : "");
    editor_set_bool_output(scene_state, outputs, "dirty_key",
                           runtime != NULL ? runtime->editor_connection_dirty : false);
    editor_set_int_output(scene_state, outputs, "revision_key",
                          runtime != NULL ? (int)runtime->editor_connection_revision : 0);
    editor_set_int_output(scene_state, outputs, "count_key", runtime != NULL ? runtime->editor_connection_count : 0);
    if (scene_state != NULL)
    {
        slayer3d_properties_set_bool(scene_state, "editor.connection.valid", ok);
        slayer3d_properties_set_string(scene_state, "editor.connection.message", message != NULL ? message : "");
        slayer3d_properties_set_int(scene_state, "editor.connection.count",
                                    runtime != NULL ? runtime->editor_connection_count : 0);
        if (ok)
        {
            slayer3d_properties_set_string(scene_state, "editor.connection.last", id != NULL ? id : "");
            slayer3d_properties_set_string(scene_state, "editor.connection.last.source", source != NULL ? source : "");
            slayer3d_properties_set_string(scene_state, "editor.connection.last.target", target != NULL ? target : "");
        }
    }
}

bool slayer3d_game_data_mark_editor_connection_source_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    char error[192];
    error[0] = '\0';
    const char *target = editor_connection_string_from_state(runtime, action, "target", "target_from_state", NULL);
    if (target == NULL || target[0] == '\0')
        target = editor_connection_selected_actor(runtime, error, sizeof(error));
    const char *event = editor_connection_string_from_state(runtime, action, "event", "event_from_state", "activate");
    if (runtime == NULL || target == NULL || target[0] == '\0' || event == NULL || event[0] == '\0')
    {
        publish_editor_connection_outputs(runtime, outputs, false,
                                          error[0] != '\0' ? error : "connection source requires actor and event", "",
                                          target, "");
        return true;
    }
    if (find_editor_actor(runtime, target) == NULL)
    {
        SDL_snprintf(error, sizeof(error), "editor actor '%s' not found", target);
        publish_editor_connection_outputs(runtime, outputs, false, error, "", target, "");
        return true;
    }

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    if (scene_state != NULL)
    {
        slayer3d_properties_set_string(scene_state, "editor.connection.source.entity", target);
        slayer3d_properties_set_string(scene_state, "editor.connection.source.event", event);
        slayer3d_properties_set_bool(scene_state, "editor.connection.source.valid", true);
    }
    publish_editor_connection_outputs(runtime, outputs, true,
                                      json_string(action, "message", "connection source marked"), "", target, "");
    return true;
}

bool slayer3d_game_data_place_editor_connection_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    char error[192];
    error[0] = '\0';
    const bool to_from_selection = json_bool(action, "to_from_selection", true);
    const char *to_entity =
        editor_connection_string_from_state(runtime, action, "to_entity", "to_entity_from_state", NULL);
    if ((to_entity == NULL || to_entity[0] == '\0') && to_from_selection)
        to_entity = editor_connection_selected_actor(runtime, error, sizeof(error));
    const char *from_entity =
        editor_connection_string_from_state(runtime, action, "from_entity", "from_entity_from_state", NULL);
    const char *from_event =
        editor_connection_string_from_state(runtime, action, "from_event", "from_event_from_state", "activate");
    const char *to_action =
        editor_connection_string_from_state(runtime, action, "to_action", "to_action_from_state", "trigger");
    if (runtime == NULL || from_entity == NULL || from_entity[0] == '\0' || to_entity == NULL || to_entity[0] == '\0' ||
        from_event == NULL || from_event[0] == '\0' || to_action == NULL || to_action[0] == '\0')
    {
        publish_editor_connection_outputs(
            runtime, outputs, false,
            error[0] != '\0' ? error : "connection requires source entity, source event, target entity, and action", "",
            from_entity, to_entity);
        return true;
    }

    slayer3d_properties *properties = properties_from_json_payload(obj_get(action, "properties"), NULL);
    if (properties == NULL)
    {
        publish_editor_connection_outputs(runtime, outputs, false, "connection properties must be an object", "",
                                          from_entity, to_entity);
        return true;
    }

    slayer3d_game_data_place_editor_connection_desc desc;
    SDL_zero(desc);
    desc.id = json_string(action, "id", NULL);
    desc.id_prefix = json_string(action, "id_prefix", "connection.editor");
    desc.from.entity = from_entity;
    desc.from.event = from_event;
    desc.from.external = json_bool(action, "from_external", false);
    desc.to.entity = to_entity;
    desc.to.action = to_action;
    desc.to.external = json_bool(action, "to_external", false);
    desc.properties = properties;
    char connection_id[160];
    const bool ok = slayer3d_game_data_place_editor_connection(runtime, &desc, connection_id, sizeof(connection_id),
                                                               error, sizeof(error));
    slayer3d_properties_destroy(properties);
    publish_editor_connection_outputs(runtime, outputs, ok,
                                      ok ? json_string(action, "message", "connection added") : error,
                                      ok ? connection_id : "", from_entity, to_entity);
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

bool slayer3d_game_data_set_editor_property_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                   const slayer3d_properties *payload)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    char error[192];
    error[0] = '\0';
    const char *target = editor_property_action_target_name(runtime, action, error, sizeof(error));
    const char *key = editor_property_action_string_from_state(runtime, action, "key", "key_from_state", NULL);
    if (runtime == NULL || target == NULL || key == NULL || key[0] == '\0')
    {
        publish_editor_property_action_outputs(
            runtime, outputs, false, error[0] != '\0' ? error : "editor property action requires a key", target, key);
        return true;
    }

    editor_actor_runtime *actor = find_editor_actor_mutable(runtime, target);
    if (actor == NULL)
    {
        SDL_snprintf(error, sizeof(error), "editor actor '%s' not found", target);
        publish_editor_property_action_outputs(runtime, outputs, false, error, target, key);
        return true;
    }

    if (actor->properties == NULL)
    {
        actor->properties = slayer3d_properties_create();
        if (actor->properties == NULL)
        {
            publish_editor_property_action_outputs(runtime, outputs, false, "failed to allocate actor properties",
                                                   target, key);
            return true;
        }
    }
    if (!editor_property_action_value(runtime, actor, key, action, payload))
    {
        publish_editor_property_action_outputs(runtime, outputs, false, "editor property value is invalid", target,
                                               key);
        return true;
    }
    mark_editor_actors_dirty(runtime);
    refresh_editor_property_selection_if_needed(runtime, target);
    publish_editor_property_action_outputs(runtime, outputs, true, json_string(action, "message", "property set"),
                                           target, key);
    return true;
}

bool slayer3d_game_data_remove_editor_property_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    char error[192];
    error[0] = '\0';
    const char *target = editor_property_action_target_name(runtime, action, error, sizeof(error));
    const char *key = editor_property_action_string_from_state(runtime, action, "key", "key_from_state", NULL);
    if (runtime == NULL || target == NULL || key == NULL || key[0] == '\0')
    {
        publish_editor_property_action_outputs(
            runtime, outputs, false, error[0] != '\0' ? error : "editor property action requires a key", target, key);
        return true;
    }

    editor_actor_runtime *actor = find_editor_actor_mutable(runtime, target);
    if (actor == NULL)
    {
        SDL_snprintf(error, sizeof(error), "editor actor '%s' not found", target);
        publish_editor_property_action_outputs(runtime, outputs, false, error, target, key);
        return true;
    }
    if (actor->properties != NULL)
        slayer3d_properties_remove(actor->properties, key);
    mark_editor_actors_dirty(runtime);
    refresh_editor_property_selection_if_needed(runtime, target);
    publish_editor_property_action_outputs(runtime, outputs, true, json_string(action, "message", "property removed"),
                                           target, key);
    return true;
}

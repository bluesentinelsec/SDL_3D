#include "game_data_internal.h"

void clear_editor_command_preview(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    SDL_zero(runtime->editor_command_preview);
    runtime->editor_command_preview.face_index = -1;
}

bool editor_command_preview_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || !runtime->editor_command_preview.active)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return active_scene != NULL && runtime->editor_command_preview.scene != NULL &&
           SDL_strcmp(runtime->editor_command_preview.scene, active_scene) == 0;
}

static bool editor_command_name_valid(const char *command)
{
    return command != NULL && (SDL_strcmp(command, "translate") == 0 || SDL_strcmp(command, "paint") == 0 ||
                               SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0 ||
                               SDL_strcmp(command, "delete") == 0);
}

static bool editor_command_target_name_valid(const char *target)
{
    return target != NULL && (SDL_strcmp(target, "selection") == 0 || SDL_strcmp(target, "world") == 0 ||
                              SDL_strcmp(target, "element") == 0 || SDL_strcmp(target, "face") == 0 ||
                              SDL_strcmp(target, "material") == 0);
}

static int find_editor_brush_material_index(const brush_world_runtime *world_runtime, const char *material_name)
{
    if (world_runtime == NULL || material_name == NULL || material_name[0] == '\0')
        return -1;
    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int i = 0; i < world->material_count; ++i)
    {
        if (world->materials[i].name != NULL && SDL_strcmp(world->materials[i].name, material_name) == 0)
            return i;
    }
    return -1;
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : NULL;
}

static bool resolve_editor_paint_material(slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_editor_selection *selection,
                                          const char *material_name, const char **out_material_name,
                                          int *out_material_index)
{
    if (out_material_name != NULL)
        *out_material_name = NULL;
    if (out_material_index != NULL)
        *out_material_index = -1;
    if (runtime == NULL || selection == NULL || selection->world_name == NULL || material_name == NULL ||
        material_name[0] == '\0')
    {
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    const int material_index = find_editor_brush_material_index(world_runtime, material_name);
    if (material_index < 0)
        return false;
    if (out_material_name != NULL)
        *out_material_name = world_runtime->desc.materials[material_index].name;
    if (out_material_index != NULL)
        *out_material_index = material_index;
    return true;
}

static bool editor_command_target_compatible(const slayer3d_game_data_editor_selection *selection, const char *command,
                                             const char *target, const char **out_reason)
{
    if (out_reason != NULL)
        *out_reason = NULL;
    if (selection == NULL || !selection->hit)
    {
        if (out_reason != NULL)
            *out_reason = "no active selection";
        return false;
    }
    if (SDL_strcmp(target, "world") == 0 && (selection->world_name == NULL || selection->world_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no world target";
        return false;
    }
    if (SDL_strcmp(target, "element") == 0 && (selection->element_name == NULL || selection->element_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no element target";
        return false;
    }
    if (SDL_strcmp(target, "face") == 0 && selection->face_index < 0)
    {
        if (out_reason != NULL)
            *out_reason = "selection has no face target";
        return false;
    }
    if (SDL_strcmp(target, "material") == 0 &&
        (selection->material_name == NULL || selection->material_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "selection has no material target";
        return false;
    }
    if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) && SDL_strcmp(target, "face") != 0)
    {
        if (out_reason != NULL)
            *out_reason = "resize/extrude target must be face";
        return false;
    }
    if ((SDL_strcmp(command, "translate") == 0 || SDL_strcmp(command, "delete") == 0) && !selection->has_bounds)
    {
        if (out_reason != NULL)
            *out_reason = "selection has no previewable bounds";
        return false;
    }
    if ((SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0) && selection->face_index < 0)
    {
        if (out_reason != NULL)
            *out_reason = "resize/extrude preview requires a face selection";
        return false;
    }
    if (SDL_strcmp(command, "paint") == 0 && selection->face_index < 0 &&
        (selection->material_name == NULL || selection->material_name[0] == '\0'))
    {
        if (out_reason != NULL)
            *out_reason = "paint preview requires a face or material selection";
        return false;
    }
    return true;
}

static slayer3d_vec3 editor_command_preview_offset(yyjson_val *action,
                                                   const slayer3d_game_data_editor_selection *selection)
{
    yyjson_val *offset = obj_get(action, "offset");
    if (offset != NULL)
        return json_vec3(action, "offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const float distance = json_float(action, "distance", 0.0f);
    if (distance != 0.0f && selection != NULL && slayer3d_vec3_length_squared(selection->normal) > 0.000001f)
        return slayer3d_vec3_scale(slayer3d_vec3_normalize(selection->normal), distance);
    return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static float editor_command_face_distance_delta(const char *command, slayer3d_vec3 offset,
                                                const slayer3d_game_data_editor_selection *selection)
{
    if (command == NULL || selection == NULL ||
        (SDL_strcmp(command, "resize") != 0 && SDL_strcmp(command, "extrude") != 0) ||
        slayer3d_vec3_length_squared(selection->normal) <= 0.000001f)
    {
        return 0.0f;
    }
    return slayer3d_vec3_dot(slayer3d_vec3_normalize(selection->normal), offset);
}

slayer3d_bounding_box editor_resized_preview_bounds(slayer3d_bounding_box bounds, slayer3d_vec3 normal, float distance)
{
    normal = slayer3d_vec3_normalize(normal);
    const float ax = SDL_fabsf(normal.x);
    const float ay = SDL_fabsf(normal.y);
    const float az = SDL_fabsf(normal.z);
    if (ax >= ay && ax >= az)
    {
        if (normal.x >= 0.0f)
            bounds.max.x += distance;
        else
            bounds.min.x -= distance;
    }
    else if (ay >= ax && ay >= az)
    {
        if (normal.y >= 0.0f)
            bounds.max.y += distance;
        else
            bounds.min.y -= distance;
    }
    else
    {
        if (normal.z >= 0.0f)
            bounds.max.z += distance;
        else
            bounds.min.z -= distance;
    }
    return bounds;
}

void publish_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *outputs, bool valid,
                                    const char *command, const char *target, const char *message,
                                    const slayer3d_game_data_editor_selection *selection,
                                    const slayer3d_bounding_box *bounds)
{
    if (runtime == NULL || outputs == NULL)
        return;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "active_key", valid);
    editor_set_bool_output(scene_state, outputs, "valid_key", valid);
    editor_set_string_output(scene_state, outputs, "command_key", command != NULL ? command : "");
    editor_set_string_output(scene_state, outputs, "target_key", target != NULL ? target : "");
    editor_set_string_output(scene_state, outputs, "message_key", message != NULL ? message : "");
    editor_set_string_output(scene_state, outputs, "world_key",
                             valid && selection != NULL && selection->world_name != NULL ? selection->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key",
                             valid && selection != NULL && selection->element_name != NULL ? selection->element_name
                                                                                           : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             valid && selection != NULL && selection->element_editor != NULL &&
                                     selection->element_editor->stable_id != NULL
                                 ? selection->element_editor->stable_id
                                 : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             valid && selection != NULL && selection->face_editor != NULL &&
                                     selection->face_editor->stable_id != NULL
                                 ? selection->face_editor->stable_id
                                 : "");
    editor_set_int_output(scene_state, outputs, "face_index_key",
                          valid && selection != NULL ? selection->face_index : -1);
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           valid && bounds != NULL ? bounds->min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           valid && bounds != NULL ? bounds->max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

bool slayer3d_game_data_preview_editor_command(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *command = json_string(action, "command", NULL);
    const char *target = json_string(action, "target", "selection");
    yyjson_val *outputs = obj_get(action, "outputs");
    if (runtime == NULL || !editor_command_name_valid(command) || !editor_command_target_name_valid(target))
        return false;

    slayer3d_game_data_editor_selection selection;
    const bool has_selection = slayer3d_game_data_get_active_editor_selection(runtime, &selection);
    const char *reason = NULL;
    const bool valid = has_selection && editor_command_target_compatible(&selection, command, target, &reason);
    if (!valid)
    {
        clear_editor_command_preview(runtime);
        publish_editor_command_preview(runtime, outputs, false, command, target, reason != NULL ? reason : "no preview",
                                       NULL, NULL);
        return true;
    }

    const slayer3d_vec3 offset = editor_command_preview_offset(action, &selection);
    const char *paint_material_name = NULL;
    int paint_material_index = -1;
    if (SDL_strcmp(command, "paint") == 0)
    {
        const char *authored_material = json_string(action, "material", NULL);
        if (!resolve_editor_paint_material(runtime, &selection, authored_material, &paint_material_name,
                                           &paint_material_index))
        {
            clear_editor_command_preview(runtime);
            publish_editor_command_preview(runtime, outputs, false, command, target, "paint preview material not found",
                                           NULL, NULL);
            return true;
        }
    }

    const bool face_resize = SDL_strcmp(command, "resize") == 0 || SDL_strcmp(command, "extrude") == 0;
    const float face_distance = editor_command_face_distance_delta(command, offset, &selection);
    slayer3d_bounding_box bounds =
        selection.has_bounds
            ? (face_resize ? editor_resized_preview_bounds(selection.bounds, selection.normal, face_distance)
                           : translated_bounds(selection.bounds, offset))
            : (slayer3d_bounding_box){selection.point, selection.point};

    editor_command_preview_state *preview = &runtime->editor_command_preview;
    SDL_zero(*preview);
    preview->active = true;
    preview->scene = slayer3d_game_data_active_scene(runtime);
    preview->command = command;
    preview->target = target;
    preview->world_name = selection.world_name;
    preview->element_name = selection.element_name;
    preview->element_stable_id = editor_metadata_stable_id(selection.element_editor);
    preview->material_name = paint_material_name != NULL ? paint_material_name : selection.material_name;
    preview->previous_material_name = selection.material_name;
    preview->face_stable_id = editor_metadata_stable_id(selection.face_editor);
    preview->face_index = selection.face_index;
    preview->material_index = paint_material_index;
    preview->previous_material_index = -1;
    if (selection.world_name != NULL && selection.material_name != NULL)
    {
        const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection.world_name);
        preview->previous_material_index = find_editor_brush_material_index(world_runtime, selection.material_name);
    }
    preview->outputs = outputs;
    preview->offset = offset;
    preview->has_bounds = true;
    preview->bounds = bounds;

    slayer3d_properties *payload = slayer3d_game_data_create_editor_selection_payload(&selection);
    char message[256];
    bool formatted = false;
    if (payload != NULL)
    {
        slayer3d_properties_set_string(payload, "editor_command", command);
        slayer3d_properties_set_string(payload, "editor_command_target", target);
        formatted = format_payload_string(payload, json_string(action, "message", "preview"), message, sizeof(message));
        slayer3d_properties_destroy(payload);
    }
    publish_editor_command_preview(runtime, outputs, true, command, target,
                                   formatted ? message : json_string(action, "message", "preview"), &selection,
                                   &bounds);
    return true;
}

bool slayer3d_game_data_clear_editor_command_preview(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    if (runtime == NULL)
        return false;
    clear_editor_command_preview(runtime);
    publish_editor_command_preview(runtime, obj_get(action, "outputs"), false, "", "",
                                   json_string(action, "message", "preview cleared"), NULL, NULL);
    return true;
}

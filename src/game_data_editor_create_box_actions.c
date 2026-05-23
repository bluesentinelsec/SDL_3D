/**
 * @file game_data_editor_create_box_actions.c
 * @brief Editor box-brush creation action helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/math.h"

bool slayer3d_game_data_create_box_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    slayer3d_game_data_create_box_brush_desc desc;
    SDL_zero(desc);
    desc.world_name = json_string(action, "world", NULL);
    desc.brush_name = json_string(action, "name", NULL);
    desc.material_name = json_string(action, "material", NULL);
    desc.contents = brush_flags_from_json(obj_get(action, "contents"), brush_content_flag_from_string, 0u);
    desc.min = json_vec3(action, "min", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    desc.max = json_vec3(action, "max", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));

    char error[256];
    error[0] = '\0';
    const char *position_from = json_string(action, "position_from", NULL);
    if (position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0)
    {
        if (!editor_placement_preview_active_for_scene(runtime) || !runtime->editor_placement_preview.has_bounds)
        {
            set_error(error, (int)sizeof(error), "box brush placement requires an active placement preview");
        }
        else
        {
            const editor_placement_preview_state *preview = &runtime->editor_placement_preview;
            const char *preview_mode = json_string(action, "preview_mode", NULL);
            if (preview_mode != NULL && preview->mode != NULL && SDL_strcmp(preview_mode, preview->mode) != 0)
            {
                set_error(error, (int)sizeof(error), "box brush placement preview mode does not match action");
            }
            else
            {
                desc.world_name = json_string(action, "world", preview->world_name);
                desc.material_name = json_string(action, "material", preview->material_name);
                if (obj_get(action, "contents") == NULL)
                    desc.contents = preview->contents;
                desc.min = preview->bounds.min;
                desc.max = preview->bounds.max;
            }
        }
    }
    else if (position_from != NULL && SDL_strcmp(position_from, "selection_point") == 0)
    {
        slayer3d_game_data_editor_selection selection;
        SDL_zero(selection);
        if (!slayer3d_game_data_get_active_editor_selection(runtime, &selection) || !selection.hit)
        {
            set_error(error, (int)sizeof(error), "box brush placement requires an active selection point");
        }
        else
        {
            slayer3d_vec3 origin = selection.point;
            origin =
                slayer3d_vec3_add(origin, json_vec3(action, "position_offset", slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
            const float snap = json_float(action, "snap", 0.0f);
            if (snap > 0.0f)
            {
                origin.x = SDL_roundf(origin.x / snap) * snap;
                origin.y = SDL_roundf(origin.y / snap) * snap;
                origin.z = SDL_roundf(origin.z / snap) * snap;
            }
            desc.min = slayer3d_vec3_add(desc.min, origin);
            desc.max = slayer3d_vec3_add(desc.max, origin);
        }
    }

    char brush_name[256];
    brush_name[0] = '\0';
    char source_warning[256];
    source_warning[0] = '\0';
    bool ok = false;
    bool source_command_applied = false;
    if (position_from != NULL && SDL_strcmp(position_from, "placement_preview") == 0)
    {
        if (error[0] == '\0' && runtime != NULL && runtime->editor_placement_preview.has_source_candidate)
        {
            const editor_placement_preview_state *preview = &runtime->editor_placement_preview;
            brush_world_runtime *world_runtime = find_brush_world_runtime_mutable(runtime, preview->world_name);
            editor_brush_source_prefab_desc source_desc;
            SDL_zero(source_desc);
            source_desc.prefab = preview->mode;
            source_desc.material = preview->material_name;
            source_desc.contents = preview->contents;
            editor_brush_source_prefab_result result;
            ok = editor_brush_world_run_source_prefab_command(world_runtime, &source_desc, desc.brush_name,
                                                              preview->source_min, preview->source_max, true, &result,
                                                              error, (int)sizeof(error));
            if (ok)
            {
                source_command_applied = !result.no_op;
                SDL_strlcpy(brush_name, result.brush_name, sizeof(brush_name));
                SDL_strlcpy(source_warning, result.warning, sizeof(source_warning));
                desc.world_name = preview->world_name;
                desc.material_name = preview->material_name;
                desc.contents = preview->contents;
                desc.min = result.bounds.min;
                desc.max = result.bounds.max;
                if (!result.no_op)
                    editor_brush_world_mark_dirty(world_runtime);
            }
        }
        else if (error[0] == '\0')
        {
            set_error(error, (int)sizeof(error),
                      "box brush placement requires a valid source-backed placement preview");
        }
    }
    else
    {
        ok = error[0] == '\0' && slayer3d_game_data_create_box_brush(runtime, &desc, brush_name, sizeof(brush_name),
                                                                     error, (int)sizeof(error));
    }
    char message[256];
    if (ok && source_warning[0] != '\0')
    {
        SDL_snprintf(message, sizeof(message), "%s; %s", json_string(action, "message", "box brush created"),
                     source_warning);
    }
    else
    {
        SDL_strlcpy(message,
                    ok ? json_string(action, "message", "box brush created")
                       : (error[0] != '\0' ? error : "box brush creation failed"),
                    sizeof(message));
    }
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key", message);
    editor_set_string_output(scene_state, outputs, "brush_key", ok ? brush_name : "");
    editor_set_vec3_output(scene_state, outputs, "bounds_min_key",
                           ok ? desc.min : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "bounds_max_key",
                           ok ? desc.max : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    (void)publish_editor_brush_world_status(runtime, outputs, desc.world_name, NULL, false);
    if (source_command_applied)
        (void)slayer3d_game_data_clear_active_editor_selection(runtime);
    return true;
}

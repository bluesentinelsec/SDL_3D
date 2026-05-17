/**
 * @file game_data_editor_active_selection.c
 * @brief Active editor selection, picking trace, and selection payload helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "game_data_brush_internal.h"
#include "slayer3d/game.h"
#include "slayer3d/math.h"

yyjson_val *active_editor_tooling_root(const slayer3d_game_data_runtime *runtime)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return obj_get(scene != NULL ? scene->root : NULL, "editor");
}

static unsigned int editor_model_filter_flag_from_string(const char *value)
{
    if (SDL_strcmp(value != NULL ? value : "", "all") == 0)
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
    if (SDL_strcmp(value != NULL ? value : "", "sector_levels") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "sector") == 0)
    {
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_SECTOR_LEVELS;
    }
    if (SDL_strcmp(value != NULL ? value : "", "brush_worlds") == 0 ||
        SDL_strcmp(value != NULL ? value : "", "brush") == 0)
    {
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_BRUSH_WORLDS;
    }
    return 0u;
}

static unsigned int editor_model_filter_from_json(yyjson_val *value)
{
    if (yyjson_is_str(value))
        return editor_model_filter_flag_from_string(yyjson_get_str(value));
    if (!yyjson_is_arr(value))
        return SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;

    unsigned int flags = 0u;
    for (size_t i = 0; i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry))
            flags |= editor_model_filter_flag_from_string(yyjson_get_str(entry));
    }
    return flags != 0u ? flags : SLAYER3D_GAME_DATA_WORLD_MODEL_FILTER_ALL;
}

static float editor_scene_state_float(const slayer3d_game_data_runtime *runtime, const char *key, float fallback)
{
    return runtime != NULL && key != NULL ? slayer3d_properties_get_float(runtime->scene_state, key, fallback)
                                          : fallback;
}

static slayer3d_vec3 editor_scene_state_vec3(const slayer3d_game_data_runtime *runtime, const char *key,
                                             slayer3d_vec3 fallback)
{
    return runtime != NULL && key != NULL ? slayer3d_properties_get_vec3(runtime->scene_state, key, fallback)
                                          : fallback;
}

static void editor_default_viewport(const slayer3d_game_data_runtime *runtime, float *out_width, float *out_height)
{
    if (out_width == NULL || out_height == NULL)
        return;

    yyjson_val *app = obj_get(runtime_root(runtime), "app");
    yyjson_val *window = obj_get(app, "window");
    const int width =
        json_int(app, "logical_width",
                 json_int(window, "logical_width", json_int(app, "width", SLAYER3D_GAME_DEFAULT_LOGICAL_WIDTH)));
    const int height =
        json_int(app, "logical_height",
                 json_int(window, "logical_height", json_int(app, "height", SLAYER3D_GAME_DEFAULT_LOGICAL_HEIGHT)));
    *out_width = (float)SDL_max(width, 1);
    *out_height = (float)SDL_max(height, 1);
}

static bool editor_trace_screen_point(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                      float viewport_width, float viewport_height, float *out_x, float *out_y)
{
    if (out_x == NULL || out_y == NULL)
        return false;

    *out_x = viewport_width * 0.5f;
    *out_y = viewport_height * 0.5f;

    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
    {
        *out_x = mouse_x;
        *out_y = mouse_y;
    }

    if (!json_vec2_value(obj_get(trace, "screen"), *out_x, *out_y, out_x, out_y))
        return false;

    *out_x = json_float(trace, "screen_x", *out_x);
    *out_y = json_float(trace, "screen_y", *out_y);
    *out_x = editor_scene_state_float(runtime, json_string(trace, "screen_x_key", NULL), *out_x);
    *out_y = editor_scene_state_float(runtime, json_string(trace, "screen_y_key", NULL), *out_y);
    return true;
}

static bool editor_trace_viewport(const slayer3d_game_data_runtime *runtime, yyjson_val *trace, float *out_width,
                                  float *out_height)
{
    editor_default_viewport(runtime, out_width, out_height);
    if (!json_vec2_value(obj_get(trace, "viewport"), *out_width, *out_height, out_width, out_height))
        return false;
    *out_width = json_float(trace, "viewport_width", *out_width);
    *out_height = json_float(trace, "viewport_height", *out_height);
    *out_width = editor_scene_state_float(runtime, json_string(trace, "viewport_width_key", NULL), *out_width);
    *out_height = editor_scene_state_float(runtime, json_string(trace, "viewport_height_key", NULL), *out_height);
    return *out_width > 0.0f && *out_height > 0.0f;
}

static bool editor_camera_screen_trace_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                                 slayer3d_game_data_world_trace_desc *out_trace)
{
    const char *camera_name = json_string(trace, "camera", slayer3d_game_data_active_camera(runtime));
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, camera_name, &camera))
        return false;

    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    if (!editor_trace_viewport(runtime, trace, &viewport_width, &viewport_height))
        return false;

    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!editor_trace_screen_point(runtime, trace, viewport_width, viewport_height, &screen_x, &screen_y))
        return false;

    const float near_distance = SDL_max(json_float(trace, "near", 0.05f), 0.0f);
    const float far_distance = SDL_max(json_float(trace, "far", 1000.0f), near_distance + 0.001f);
    return slayer3d_camera3d_screen_ray(&camera, viewport_width, viewport_height, screen_x, screen_y, near_distance,
                                        far_distance, &out_trace->start, &out_trace->end);
}

bool editor_trace_desc_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                 slayer3d_game_data_world_trace_desc *out_trace)
{
    if (out_trace != NULL)
        SDL_zero(*out_trace);
    yyjson_val *trace = obj_get(selection, "trace");
    if (!yyjson_is_obj(trace) || out_trace == NULL)
        return false;

    out_trace->shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
    const char *source = json_string(trace, "source", "world");
    if (SDL_strcmp(source, "camera_screen") == 0)
    {
        if (!editor_camera_screen_trace_from_json(runtime, trace, out_trace))
            return false;
    }
    else
    {
        out_trace->start = json_vec3(trace, "start", slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        out_trace->end = json_vec3(trace, "end", out_trace->start);
    }
    out_trace->contents_mask =
        brush_flags_from_json(obj_get(trace, "contents_mask"), brush_content_flag_from_string,
                              SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PLAYER_CLIP);
    out_trace->model_filter = editor_model_filter_from_json(obj_get(trace, "model_filter"));
    return true;
}

static bool editor_work_plane_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *work_plane,
                                        slayer3d_vec3 *out_normal, float *out_distance)
{
    if (!yyjson_is_obj(work_plane) || !json_bool(work_plane, "enabled", true) || out_normal == NULL ||
        out_distance == NULL)
    {
        return false;
    }

    slayer3d_vec3 normal = json_vec3(work_plane, "normal", slayer3d_vec3_make(0.0f, 1.0f, 0.0f));
    normal = editor_scene_state_vec3(runtime, json_string(work_plane, "normal_key", NULL), normal);
    if (slayer3d_vec3_length_squared(normal) <= 0.000001f)
        return false;

    *out_normal = slayer3d_vec3_normalize(normal);
    *out_distance = editor_scene_state_float(runtime, json_string(work_plane, "distance_key", NULL),
                                             json_float(work_plane, "distance", 0.0f));
    return true;
}

static bool editor_work_plane_selection_from_trace(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                                   const slayer3d_game_data_world_trace_desc *trace,
                                                   slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    yyjson_val *work_plane = obj_get(obj_get(selection, "trace"), "work_plane");
    if (trace == NULL || out_selection == NULL)
    {
        return false;
    }

    slayer3d_vec3 normal;
    float distance = 0.0f;
    if (!editor_work_plane_from_json(runtime, work_plane, &normal, &distance))
        return false;

    const slayer3d_vec3 direction = slayer3d_vec3_sub(trace->end, trace->start);
    const float denominator = slayer3d_vec3_dot(normal, direction);
    if (SDL_fabsf(denominator) <= 0.000001f)
        return false;

    const float fraction = (distance - slayer3d_vec3_dot(normal, trace->start)) / denominator;
    if (fraction < 0.0f || fraction > 1.0f)
        return false;

    out_selection->hit = true;
    out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_INVALID;
    out_selection->element_index = -1;
    out_selection->face_index = -1;
    out_selection->fraction = fraction;
    out_selection->point = slayer3d_vec3_add(trace->start, slayer3d_vec3_scale(direction, fraction));
    out_selection->normal = normal;
    return true;
}

bool editor_work_plane_desc_from_trace_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                            slayer3d_vec3 *out_normal, float *out_distance)
{
    return editor_work_plane_from_json(runtime, obj_get(trace, "work_plane"), out_normal, out_distance);
}

bool editor_pick_selection_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                     const slayer3d_game_data_world_trace_desc *trace,
                                     slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || selection == NULL || trace == NULL || out_selection == NULL)
        return false;

    if (!slayer3d_game_data_pick_editor_world_model(runtime, trace, out_selection))
        return editor_work_plane_selection_from_trace(runtime, selection, trace, out_selection);
    if (out_selection->hit)
        return true;
    if (editor_work_plane_selection_from_trace(runtime, selection, trace, out_selection))
        return true;
    return true;
}

bool editor_selection_mode_is_click(yyjson_val *selection)
{
    const char *mode = json_string(selection, "mode", "hover");
    return SDL_strcmp(mode, "click") == 0;
}

bool editor_selection_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selection_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selection_scene, active_scene) == 0;
}

static void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
}

static bool editor_selection_select_requested(const slayer3d_game_data_runtime *runtime, yyjson_val *selection)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;

    const Uint8 button = mouse_button_from_json(json_string(selection, "select_button", "LEFT"));
    return button != 0 && slayer3d_input_get_pressed_mouse_button(input) == button;
}

static const char *editor_selection_type_name(slayer3d_game_data_world_model_type type)
{
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
        return "sector_level";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return "brush_world";
    return "none";
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || outputs == NULL || selection == NULL)
        return;

    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "hit_key", selection->hit);
    editor_set_string_output(scene_state, outputs, "type_key", editor_selection_type_name(selection->type));
    editor_set_string_output(scene_state, outputs, "world_key", selection->hit ? selection->world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key", selection->hit ? selection->element_name : "");
    editor_set_string_output(scene_state, outputs, "material_key", selection->hit ? selection->material_name : "");
    editor_set_string_output(scene_state, outputs, "world_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->world_editor) : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->element_editor) : "");
    editor_set_string_output(scene_state, outputs, "material_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->material_editor) : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             selection->hit ? editor_metadata_stable_id(selection->face_editor) : "");
    editor_set_int_output(scene_state, outputs, "element_index_key", selection->hit ? selection->element_index : -1);
    editor_set_int_output(scene_state, outputs, "face_index_key", selection->hit ? selection->face_index : -1);
    editor_set_float_output(scene_state, outputs, "fraction_key", selection->hit ? selection->fraction : 1.0f);
    editor_set_vec3_output(scene_state, outputs, "point_key",
                           selection->hit ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "normal_key",
                           selection->hit ? selection->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
}

bool slayer3d_game_data_update_active_editor_tooling(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    yyjson_val *editor = active_editor_tooling_root(runtime);
    yyjson_val *selection_json = obj_get(editor, "selection");
    yyjson_val *outputs = obj_get(selection_json, "outputs");
    if (!yyjson_is_obj(selection_json))
    {
        clear_editor_active_selection(runtime);
        return true;
    }
    if (!editor_selection_active_for_scene(runtime))
        clear_editor_active_selection(runtime);

    slayer3d_game_data_world_trace_desc trace;
    slayer3d_game_data_editor_selection hover_selection;
    init_editor_selection(&hover_selection);
    if (!editor_trace_desc_from_json(runtime, selection_json, &trace) ||
        !editor_pick_selection_from_json(runtime, selection_json, &trace, &hover_selection))
    {
        return false;
    }

    if (!editor_selection_mode_is_click(selection_json))
    {
        runtime->editor_active_selection = hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        update_editor_placement_preview(runtime, editor, &hover_selection);
        publish_editor_selection(runtime, outputs, &hover_selection);
        return true;
    }

    publish_editor_selection(runtime, obj_get(selection_json, "hover_outputs"), &hover_selection);
    update_editor_placement_preview(runtime, editor, &hover_selection);
    if (editor_selection_select_requested(runtime, selection_json))
    {
        if (hover_selection.hit)
            runtime->editor_active_selection = hover_selection;
        else if (json_bool(selection_json, "clear_on_miss", true))
            init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }

    publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
    return true;
}

bool slayer3d_game_data_get_active_editor_selection(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_selection *out_selection)
{
    if (out_selection != NULL)
        init_editor_selection(out_selection);
    if (runtime == NULL || out_selection == NULL || !editor_selection_active_for_scene(runtime) ||
        !runtime->editor_active_selection.hit)
    {
        return false;
    }
    *out_selection = runtime->editor_active_selection;
    return true;
}

bool slayer3d_game_data_clear_active_editor_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    clear_editor_active_selection(runtime);
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    return true;
}

slayer3d_properties *slayer3d_game_data_create_editor_selection_payload(
    const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;

    const bool hit = selection != NULL && selection->hit;
    slayer3d_properties_set_bool(payload, "selection_hit", hit);
    slayer3d_properties_set_string(payload, "selection_type",
                                   hit ? editor_selection_type_name(selection->type) : "none");
    slayer3d_properties_set_string(payload, "selection_world",
                                   hit && selection->world_name != NULL ? selection->world_name : "");
    slayer3d_properties_set_string(payload, "selection_element",
                                   hit && selection->element_name != NULL ? selection->element_name : "");
    slayer3d_properties_set_string(payload, "selection_material",
                                   hit && selection->material_name != NULL ? selection->material_name : "");
    slayer3d_properties_set_string(payload, "selection_world_stable_id",
                                   hit ? editor_metadata_stable_id(selection->world_editor) : "");
    slayer3d_properties_set_string(payload, "selection_element_stable_id",
                                   hit ? editor_metadata_stable_id(selection->element_editor) : "");
    slayer3d_properties_set_string(payload, "selection_material_stable_id",
                                   hit ? editor_metadata_stable_id(selection->material_editor) : "");
    slayer3d_properties_set_string(payload, "selection_face_stable_id",
                                   hit ? editor_metadata_stable_id(selection->face_editor) : "");
    slayer3d_properties_set_int(payload, "selection_element_index", hit ? selection->element_index : -1);
    slayer3d_properties_set_int(payload, "selection_face_index", hit ? selection->face_index : -1);
    slayer3d_properties_set_float(payload, "selection_fraction", hit ? selection->fraction : 1.0f);
    slayer3d_properties_set_vec3(payload, "selection_world_position",
                                 hit ? selection->world_position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_point",
                                 hit ? selection->point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(payload, "selection_normal",
                                 hit ? selection->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    return payload;
}

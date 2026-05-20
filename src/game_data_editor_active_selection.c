/**
 * @file game_data_editor_active_selection.c
 * @brief Active editor selection, picking trace, and selection payload helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_keyboard.h>
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

typedef struct editor_trace_viewport_config
{
    const char *camera;
    float x;
    float y;
    float width;
    float height;
    float screen_x;
    float screen_y;
    yyjson_val *work_plane;
} editor_trace_viewport_config;

static bool editor_trace_screen_value_is_center(yyjson_val *value)
{
    return yyjson_is_str(value) && SDL_strcmp(yyjson_get_str(value), "center") == 0;
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

    yyjson_val *screen = obj_get(trace, "screen");
    if (!editor_trace_screen_value_is_center(screen))
    {
        slayer3d_input_manager *input = runtime_input(runtime);
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        if (slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        {
            *out_x = mouse_x;
            *out_y = mouse_y;
        }
    }

    if (!editor_trace_screen_value_is_center(screen) && !json_vec2_value(screen, *out_x, *out_y, out_x, out_y))
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

static bool editor_trace_viewport_rect(yyjson_val *viewport, float *out_x, float *out_y, float *out_w, float *out_h)
{
    yyjson_val *rect = obj_get(viewport, "rect");
    if (!yyjson_is_arr(rect) || yyjson_arr_size(rect) != 4 || out_x == NULL || out_y == NULL || out_w == NULL ||
        out_h == NULL)
    {
        return false;
    }
    *out_x = (float)yyjson_get_num(yyjson_arr_get(rect, 0));
    *out_y = (float)yyjson_get_num(yyjson_arr_get(rect, 1));
    *out_w = (float)yyjson_get_num(yyjson_arr_get(rect, 2));
    *out_h = (float)yyjson_get_num(yyjson_arr_get(rect, 3));
    return *out_w > 0.0f && *out_h > 0.0f;
}

static bool editor_trace_select_viewport(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                         editor_trace_viewport_config *out_viewport)
{
    if (runtime == NULL || trace == NULL || out_viewport == NULL)
        return false;
    yyjson_val *viewports = obj_get(trace, "viewports");
    if (!yyjson_is_arr(viewports))
        return false;

    float full_width = 0.0f;
    float full_height = 0.0f;
    if (!editor_trace_viewport(runtime, trace, &full_width, &full_height))
        return false;
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!editor_trace_screen_point(runtime, trace, full_width, full_height, &screen_x, &screen_y))
        return false;

    for (size_t i = 0; i < yyjson_arr_size(viewports); ++i)
    {
        yyjson_val *viewport = yyjson_arr_get(viewports, i);
        yyjson_val *active_if = obj_get(viewport, "active_if");
        if (active_if != NULL && !eval_data_condition(runtime, active_if, NULL))
            continue;

        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        if (!editor_trace_viewport_rect(viewport, &x, &y, &width, &height))
            continue;

        const bool center_screen = editor_trace_screen_value_is_center(obj_get(viewport, "screen"));
        const float candidate_screen_x = center_screen ? x + width * 0.5f : screen_x;
        const float candidate_screen_y = center_screen ? y + height * 0.5f : screen_y;
        if (candidate_screen_x < x || candidate_screen_y < y || candidate_screen_x >= x + width ||
            candidate_screen_y >= y + height)
        {
            continue;
        }

        SDL_zero(*out_viewport);
        out_viewport->camera = json_string(viewport, "camera", NULL);
        out_viewport->x = x;
        out_viewport->y = y;
        out_viewport->width = width;
        out_viewport->height = height;
        out_viewport->screen_x = candidate_screen_x - x;
        out_viewport->screen_y = candidate_screen_y - y;
        if (!center_screen &&
            !json_vec2_value(obj_get(viewport, "screen"), out_viewport->screen_x, out_viewport->screen_y,
                             &out_viewport->screen_x, &out_viewport->screen_y))
        {
            return false;
        }
        out_viewport->work_plane = obj_get(viewport, "work_plane");
        return out_viewport->camera != NULL;
    }
    return false;
}

static bool editor_camera_screen_trace_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                                 slayer3d_game_data_world_trace_desc *out_trace)
{
    editor_trace_viewport_config selected_viewport;
    const bool has_selected_viewport = editor_trace_select_viewport(runtime, trace, &selected_viewport);
    const char *camera_name = has_selected_viewport
                                  ? selected_viewport.camera
                                  : json_string(trace, "camera", slayer3d_game_data_active_camera(runtime));
    slayer3d_camera3d camera;
    if (!slayer3d_game_data_get_camera(runtime, camera_name, &camera))
        return false;

    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    if (!editor_trace_viewport(runtime, trace, &viewport_width, &viewport_height))
        return false;
    if (has_selected_viewport)
    {
        viewport_width = selected_viewport.width;
        viewport_height = selected_viewport.height;
    }

    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!editor_trace_screen_point(runtime, trace, viewport_width, viewport_height, &screen_x, &screen_y))
        return false;
    if (has_selected_viewport)
    {
        screen_x = selected_viewport.screen_x;
        screen_y = selected_viewport.screen_y;
    }

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
    yyjson_val *trace_json = obj_get(selection, "trace");
    editor_trace_viewport_config selected_viewport;
    const bool has_selected_viewport = editor_trace_select_viewport(runtime, trace_json, &selected_viewport);
    yyjson_val *work_plane = has_selected_viewport && yyjson_is_obj(selected_viewport.work_plane)
                                 ? selected_viewport.work_plane
                                 : obj_get(trace_json, "work_plane");
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
    editor_trace_viewport_config selected_viewport;
    const bool has_selected_viewport = editor_trace_select_viewport(runtime, trace, &selected_viewport);
    return editor_work_plane_from_json(runtime,
                                       has_selected_viewport && yyjson_is_obj(selected_viewport.work_plane)
                                           ? selected_viewport.work_plane
                                           : obj_get(trace, "work_plane"),
                                       out_normal, out_distance);
}

bool editor_pick_selection_from_json(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                     const slayer3d_game_data_world_trace_desc *trace,
                                     slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || selection == NULL || trace == NULL || out_selection == NULL)
        return false;

    slayer3d_game_data_editor_selection world_selection;
    slayer3d_game_data_editor_selection player_start_selection;
    init_editor_selection(&world_selection);
    init_editor_selection(&player_start_selection);
    if (!slayer3d_game_data_pick_editor_world_model(runtime, trace, &world_selection))
        return editor_work_plane_selection_from_trace(runtime, selection, trace, out_selection);
    const bool player_start_hit = pick_editor_player_start(runtime, trace, &player_start_selection);
    if (world_selection.hit && player_start_hit)
    {
        *out_selection =
            player_start_selection.fraction <= world_selection.fraction ? player_start_selection : world_selection;
        return true;
    }
    if (player_start_hit)
    {
        *out_selection = player_start_selection;
        return true;
    }
    if (world_selection.hit)
    {
        *out_selection = world_selection;
        return true;
    }
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

static bool editor_selected_brushes_active_for_scene(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return false;
    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    return runtime->editor_selected_brush_scene != NULL && active_scene != NULL &&
           SDL_strcmp(runtime->editor_selected_brush_scene, active_scene) == 0;
}

static void publish_editor_selected_brush_count(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return;
    const int count = editor_selected_brushes_active_for_scene(runtime) ? runtime->editor_selected_brush_count : 0;
    slayer3d_properties_set_int(runtime->scene_state, "editor.selection.count", count);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.selection.multiple", count > 1);
}

static void clear_editor_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < SLAYER3D_EDITOR_SELECTED_BRUSH_CAPACITY; ++i)
        init_editor_selection(&runtime->editor_selected_brushes[i]);
    runtime->editor_selected_brush_count = 0;
    runtime->editor_selected_brush_scene = slayer3d_game_data_active_scene(runtime);
    publish_editor_selected_brush_count(runtime);
}

static void clear_editor_active_selection(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    clear_editor_selected_brushes(runtime);
    clear_editor_command_preview(runtime);
    clear_editor_placement_preview(runtime);
}

static bool editor_selection_button_requested(const slayer3d_game_data_runtime *runtime, yyjson_val *selection,
                                              const char *key, const char *fallback)
{
    slayer3d_input_manager *input = runtime_input(runtime);
    if (input == NULL)
        return false;

    const Uint8 button = mouse_button_from_json(json_string(selection, key, fallback));
    return button != 0 && slayer3d_input_get_pressed_mouse_button(input) == button;
}

static slayer3d_game_data_editor_selection resolved_editor_selection(
    const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *selection);

static bool emit_editor_selection_signal(slayer3d_game_data_runtime *runtime, yyjson_val *selection_json,
                                         const char *signal_key, const slayer3d_game_data_editor_selection *selection)
{
    const char *signal = json_string(selection_json, signal_key, NULL);
    if (signal == NULL)
        return true;

    slayer3d_signal_bus *bus = runtime_bus(runtime);
    const int signal_id = slayer3d_game_data_find_signal(runtime, signal);
    if (bus == NULL || signal_id < 0)
        return false;

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    slayer3d_properties *payload = slayer3d_game_data_create_editor_selection_payload(&resolved);
    if (payload == NULL)
        return false;
    slayer3d_signal_emit(bus, signal_id, payload);
    slayer3d_properties_destroy(payload);
    return true;
}

static const char *editor_selection_type_name(slayer3d_game_data_world_model_type type)
{
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_SECTOR_LEVEL)
        return "sector_level";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD)
        return "brush_world";
    if (type == SLAYER3D_GAME_DATA_WORLD_MODEL_EDITOR_PLAYER_START)
        return "editor_player_start";
    return "none";
}

static bool editor_selection_is_selectable_brush(const slayer3d_game_data_editor_selection *selection)
{
    return selection != NULL && selection->hit && selection->type == SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD &&
           selection->world_name != NULL && selection->world_name[0] != '\0' && selection->element_name != NULL &&
           selection->element_name[0] != '\0';
}

static int editor_selected_brush_index(const slayer3d_game_data_runtime *runtime,
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

static void remove_editor_selected_brush_at(slayer3d_game_data_runtime *runtime, int index)
{
    if (runtime == NULL || index < 0 || index >= runtime->editor_selected_brush_count)
        return;
    for (int i = index; i + 1 < runtime->editor_selected_brush_count; ++i)
        runtime->editor_selected_brushes[i] = runtime->editor_selected_brushes[i + 1];
    runtime->editor_selected_brush_count--;
    init_editor_selection(&runtime->editor_selected_brushes[runtime->editor_selected_brush_count]);
    publish_editor_selected_brush_count(runtime);
}

static bool add_editor_selected_brush(slayer3d_game_data_runtime *runtime,
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

static void update_active_editor_selection_from_selected_brushes(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    if (editor_selected_brushes_active_for_scene(runtime) && runtime->editor_selected_brush_count > 0)
        runtime->editor_active_selection = runtime->editor_selected_brushes[runtime->editor_selected_brush_count - 1];
    else
        init_editor_selection(&runtime->editor_active_selection);
    runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
}

static bool editor_select_mode_primary_click(slayer3d_game_data_runtime *runtime,
                                             const slayer3d_game_data_editor_selection *hover_selection)
{
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (!editor_selection_is_selectable_brush(hover_selection))
    {
        if (!shift)
            clear_editor_selected_brushes(runtime);
        runtime->editor_active_selection = *hover_selection;
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
        return true;
    }

    const int selected_index = editor_selected_brush_index(runtime, hover_selection);
    if (selected_index >= 0)
    {
        remove_editor_selected_brush_at(runtime, selected_index);
        update_active_editor_selection_from_selected_brushes(runtime);
        return true;
    }

    if (!shift)
        clear_editor_selected_brushes(runtime);
    if (!add_editor_selected_brush(runtime, hover_selection))
        return false;
    update_active_editor_selection_from_selected_brushes(runtime);
    return true;
}

static bool editor_select_mode_secondary_click(slayer3d_game_data_runtime *runtime,
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

static bool editor_mode_is_select(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->scene_state != NULL &&
           SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", "select"), "select") == 0;
}

static const char *editor_metadata_stable_id(const slayer3d_game_data_editor_metadata *metadata)
{
    return metadata != NULL && metadata->stable_id != NULL ? metadata->stable_id : "";
}

static int editor_box_face_key_index(const char *face_key)
{
    static const char *const face_keys[] = {"px", "nx", "py", "ny", "pz", "nz"};
    for (size_t i = 0; face_key != NULL && i < SDL_arraysize(face_keys); ++i)
    {
        if (SDL_strcmp(face_key, face_keys[i]) == 0)
            return (int)i;
    }
    return -1;
}

static slayer3d_vec3 editor_selection_face_center(const slayer3d_game_data_brush *brush, int face_index)
{
    if (brush == NULL || !brush->has_bounds)
        return slayer3d_vec3_make(0.0f, 0.0f, 0.0f);

    slayer3d_vec3 center = slayer3d_vec3_scale(slayer3d_vec3_add(brush->bounds.min, brush->bounds.max), 0.5f);
    if (face_index == 0)
        center.x = brush->bounds.max.x;
    else if (face_index == 1)
        center.x = brush->bounds.min.x;
    else if (face_index == 2)
        center.y = brush->bounds.max.y;
    else if (face_index == 3)
        center.y = brush->bounds.min.y;
    else if (face_index == 4)
        center.z = brush->bounds.max.z;
    else if (face_index == 5)
        center.z = brush->bounds.min.z;
    return center;
}

static void resolve_brush_editor_selection_metadata(const slayer3d_game_data_runtime *runtime,
                                                    slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || selection == NULL || !editor_selection_is_selectable_brush(selection))
        return;

    selection->world_editor = NULL;
    selection->element_editor = NULL;
    selection->face_editor = NULL;
    selection->material_editor = NULL;

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, selection->world_name);
    if (world_runtime == NULL)
        return;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    selection->world_editor = &world->editor;
    selection->element_index = -1;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (brush->name == NULL || SDL_strcmp(brush->name, selection->element_name) != 0)
            continue;

        selection->element_index = brush_index;
        selection->element_editor = &brush->editor;
        selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            selection->bounds = brush->bounds;

        if (selection->face_index >= 0 && selection->face_index < brush->face_count)
        {
            const slayer3d_game_data_brush_face *face = &brush->faces[selection->face_index];
            selection->face_editor = &face->editor;
            selection->material_name = face->material_name;
            if (face->material_index >= 0 && face->material_index < world->material_count)
                selection->material_editor = &world->materials[face->material_index].editor;
        }
        return;
    }
}

static slayer3d_game_data_editor_selection resolved_editor_selection(
    const slayer3d_game_data_runtime *runtime, const slayer3d_game_data_editor_selection *selection)
{
    slayer3d_game_data_editor_selection resolved;
    init_editor_selection(&resolved);
    if (selection == NULL)
        return resolved;
    resolved = *selection;
    resolve_brush_editor_selection_metadata(runtime, &resolved);
    return resolved;
}

static bool editor_select_brush_by_name(slayer3d_game_data_runtime *runtime, const char *world_name,
                                        const char *brush_name, const char *face_key,
                                        slayer3d_game_data_editor_selection *out_selection)
{
    init_editor_selection(out_selection);
    if (runtime == NULL || world_name == NULL || world_name[0] == '\0' || brush_name == NULL || brush_name[0] == '\0' ||
        out_selection == NULL)
    {
        return false;
    }

    const brush_world_runtime *world_runtime = find_brush_world_runtime(runtime, world_name);
    if (world_runtime == NULL)
        return false;

    const slayer3d_game_data_brush_world *world = &world_runtime->desc;
    for (int brush_index = 0; brush_index < world->brush_count; ++brush_index)
    {
        const slayer3d_game_data_brush *brush = &world->brushes[brush_index];
        if (brush->name == NULL || SDL_strcmp(brush->name, brush_name) != 0)
            continue;

        const int face_index = editor_box_face_key_index(face_key);
        const slayer3d_game_data_brush_face *face =
            face_index >= 0 && face_index < brush->face_count ? &brush->faces[face_index] : NULL;

        out_selection->hit = true;
        out_selection->type = SLAYER3D_GAME_DATA_WORLD_MODEL_BRUSH_WORLD;
        out_selection->world_name = world->name;
        out_selection->world_position = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        out_selection->element_name = brush->name;
        out_selection->material_name = face != NULL ? face->material_name : NULL;
        out_selection->element_index = brush_index;
        out_selection->face_index = face != NULL ? face_index : -1;
        out_selection->fraction = 0.0f;
        out_selection->point = editor_selection_face_center(brush, face_index);
        out_selection->normal = face != NULL ? slayer3d_vec3_normalize(face->normal) : slayer3d_vec3_make(0, 1, 0);
        out_selection->has_bounds = brush->has_bounds;
        if (brush->has_bounds)
            out_selection->bounds = brush->bounds;
        resolve_brush_editor_selection_metadata(runtime, out_selection);
        return true;
    }
    return false;
}

bool slayer3d_game_data_select_editor_brush_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *outputs = obj_get(action, "outputs");
    const char *world_name = json_string(action, "world", NULL);
    const char *brush_name = json_string(action, "element", NULL);
    const char *brush_name_key = json_string(action, "element_from_state", NULL);
    const char *face_key = json_string(action, "face", NULL);
    const char *face_from_state = json_string(action, "face_from_state", NULL);
    if (runtime != NULL && runtime->scene_state != NULL)
    {
        if ((brush_name == NULL || brush_name[0] == '\0') && brush_name_key != NULL && brush_name_key[0] != '\0')
            brush_name = slayer3d_properties_get_string(runtime->scene_state, brush_name_key, "");
        if ((face_key == NULL || face_key[0] == '\0') && face_from_state != NULL && face_from_state[0] != '\0')
            face_key = slayer3d_properties_get_string(runtime->scene_state, face_from_state, "");
    }

    slayer3d_game_data_editor_selection selection;
    const bool ok = editor_select_brush_by_name(runtime, world_name, brush_name, face_key, &selection);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "valid_key", ok);
    editor_set_string_output(scene_state, outputs, "message_key",
                             ok ? json_string(action, "message", "brush selected")
                                : json_string(action, "invalid_message", "brush selection failed"));
    yyjson_val *selection_json = obj_get(active_editor_tooling_root(runtime), "selection");
    if (ok)
    {
        clear_editor_selected_brushes(runtime);
        (void)add_editor_selected_brush(runtime, &selection);
        update_active_editor_selection_from_selected_brushes(runtime);
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    else
    {
        clear_editor_active_selection(runtime);
        publish_editor_selection(runtime, obj_get(selection_json, "outputs"), &runtime->editor_active_selection);
    }
    return true;
}

void publish_editor_selection(slayer3d_game_data_runtime *runtime, yyjson_val *outputs,
                              const slayer3d_game_data_editor_selection *selection)
{
    if (runtime == NULL || outputs == NULL || selection == NULL)
        return;

    slayer3d_game_data_editor_selection resolved = resolved_editor_selection(runtime, selection);
    slayer3d_properties *scene_state = slayer3d_game_data_mutable_scene_state(runtime);
    editor_set_bool_output(scene_state, outputs, "hit_key", resolved.hit);
    editor_set_string_output(scene_state, outputs, "type_key", editor_selection_type_name(resolved.type));
    editor_set_string_output(scene_state, outputs, "world_key", resolved.hit ? resolved.world_name : "");
    editor_set_string_output(scene_state, outputs, "element_key", resolved.hit ? resolved.element_name : "");
    editor_set_string_output(scene_state, outputs, "material_key", resolved.hit ? resolved.material_name : "");
    editor_set_string_output(scene_state, outputs, "world_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.world_editor) : "");
    editor_set_string_output(scene_state, outputs, "element_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.element_editor) : "");
    editor_set_string_output(scene_state, outputs, "material_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.material_editor) : "");
    editor_set_string_output(scene_state, outputs, "face_stable_id_key",
                             resolved.hit ? editor_metadata_stable_id(resolved.face_editor) : "");
    editor_set_int_output(scene_state, outputs, "element_index_key", resolved.hit ? resolved.element_index : -1);
    editor_set_int_output(scene_state, outputs, "face_index_key", resolved.hit ? resolved.face_index : -1);
    editor_set_float_output(scene_state, outputs, "fraction_key", resolved.hit ? resolved.fraction : 1.0f);
    editor_set_vec3_output(scene_state, outputs, "point_key",
                           resolved.hit ? resolved.point : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    editor_set_vec3_output(scene_state, outputs, "normal_key",
                           resolved.hit ? resolved.normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
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
    const bool select_requested = editor_selection_button_requested(runtime, selection_json, "select_button", "LEFT");
    const bool secondary_select_requested =
        editor_selection_button_requested(runtime, selection_json, "secondary_select_button", NULL);
    if (editor_mode_is_select(runtime))
    {
        if (select_requested)
        {
            if (hover_selection.hit)
            {
                if (!editor_select_mode_primary_click(runtime, &hover_selection))
                    return false;
            }
            else if (json_bool(selection_json, "clear_on_miss", true))
            {
                clear_editor_active_selection(runtime);
            }
        }
        if (secondary_select_requested && hover_selection.hit)
        {
            if (!editor_select_mode_secondary_click(runtime, &hover_selection))
                return false;
        }
        publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
        publish_editor_selected_brush_count(runtime);
        if (secondary_select_requested && !emit_editor_selection_signal(runtime, selection_json, "on_secondary_select",
                                                                        &runtime->editor_active_selection))
        {
            return false;
        }
        return true;
    }

    if (select_requested || secondary_select_requested)
    {
        clear_editor_selected_brushes(runtime);
        if (hover_selection.hit)
            runtime->editor_active_selection = hover_selection;
        else if (json_bool(selection_json, "clear_on_miss", true))
            init_editor_selection(&runtime->editor_active_selection);
        runtime->editor_selection_scene = slayer3d_game_data_active_scene(runtime);
    }

    publish_editor_selection(runtime, outputs, &runtime->editor_active_selection);
    publish_editor_selected_brush_count(runtime);
    if (secondary_select_requested && !emit_editor_selection_signal(runtime, selection_json, "on_secondary_select",
                                                                    &runtime->editor_active_selection))
    {
        return false;
    }
    if (select_requested &&
        !emit_editor_selection_signal(runtime, selection_json, "on_select", &runtime->editor_active_selection))
    {
        return false;
    }
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

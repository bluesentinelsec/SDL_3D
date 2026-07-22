/**
 * @file game_data_editor_trace.c
 * @brief Editor picking trace, viewport, and work-plane helpers.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/camera.h"
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

static bool editor_trace_uses_scene_viewports(yyjson_val *trace)
{
    yyjson_val *viewports = obj_get(trace, "viewports");
    return yyjson_is_str(viewports) && SDL_strcmp(yyjson_get_str(viewports), "scene") == 0;
}

static bool editor_trace_select_scene_viewport_at(const slayer3d_game_data_runtime *runtime, float screen_x,
                                                  float screen_y, editor_trace_viewport_config *out_viewport)
{
    game_data_scene_world_viewport viewports[SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX];
    int count = 0;
    if (!game_data_resolve_active_scene_world_viewports(runtime, viewports, SDL_arraysize(viewports), &count))
        return false;

    for (int i = 0; i < count; ++i)
    {
        const SDL_Rect rect = viewports[i].rect;
        if (screen_x < (float)rect.x || screen_y < (float)rect.y || screen_x >= (float)(rect.x + rect.w) ||
            screen_y >= (float)(rect.y + rect.h))
        {
            continue;
        }

        SDL_zero(*out_viewport);
        out_viewport->camera = viewports[i].camera;
        out_viewport->x = (float)rect.x;
        out_viewport->y = (float)rect.y;
        out_viewport->width = (float)rect.w;
        out_viewport->height = (float)rect.h;
        out_viewport->screen_x = screen_x - (float)rect.x;
        out_viewport->screen_y = screen_y - (float)rect.y;
        out_viewport->work_plane = viewports[i].work_plane;
        return true;
    }
    return false;
}

static bool editor_trace_select_viewport(const slayer3d_game_data_runtime *runtime, yyjson_val *trace,
                                         editor_trace_viewport_config *out_viewport)
{
    if (runtime == NULL || trace == NULL || out_viewport == NULL)
        return false;
    yyjson_val *viewports = obj_get(trace, "viewports");
    if (!yyjson_is_arr(viewports) && !editor_trace_uses_scene_viewports(trace))
        return false;

    float full_width = 0.0f;
    float full_height = 0.0f;
    if (!editor_trace_viewport(runtime, trace, &full_width, &full_height))
        return false;
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!editor_trace_screen_point(runtime, trace, full_width, full_height, &screen_x, &screen_y))
        return false;

    if (editor_trace_uses_scene_viewports(trace))
        return editor_trace_select_scene_viewport_at(runtime, screen_x, screen_y, out_viewport);

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

bool editor_trace_select_viewport_at(const slayer3d_game_data_runtime *runtime, yyjson_val *trace, float screen_x,
                                     float screen_y, editor_trace_viewport_config *out_viewport)
{
    if (runtime == NULL || trace == NULL || out_viewport == NULL)
        return false;
    yyjson_val *viewports = obj_get(trace, "viewports");
    if (editor_trace_uses_scene_viewports(trace))
        return editor_trace_select_scene_viewport_at(runtime, screen_x, screen_y, out_viewport);
    if (!yyjson_is_arr(viewports))
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
        if (screen_x < x || screen_y < y || screen_x >= x + width || screen_y >= y + height)
            continue;

        SDL_zero(*out_viewport);
        out_viewport->camera = json_string(viewport, "camera", NULL);
        out_viewport->x = x;
        out_viewport->y = y;
        out_viewport->width = width;
        out_viewport->height = height;
        out_viewport->screen_x = screen_x - x;
        out_viewport->screen_y = screen_y - y;
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
    slayer3d_game_data_editor_selection actor_selection;
    init_editor_selection(&world_selection);
    init_editor_selection(&player_start_selection);
    init_editor_selection(&actor_selection);
    (void)slayer3d_game_data_pick_editor_world_model(runtime, trace, &world_selection);
    const bool player_start_hit = pick_editor_player_start(runtime, trace, &player_start_selection);
    const bool actor_hit = pick_editor_actor(runtime, trace, &actor_selection);
    const slayer3d_game_data_editor_selection *best = NULL;
    if (world_selection.hit)
        best = &world_selection;
    if (player_start_hit)
        best = best == NULL || player_start_selection.fraction <= best->fraction ? &player_start_selection : best;
    if (actor_hit)
        best = best == NULL || actor_selection.fraction <= best->fraction ? &actor_selection : best;
    if (best != NULL)
    {
        *out_selection = *best;
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

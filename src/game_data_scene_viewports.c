/**
 * @file game_data_scene_viewports.c
 * @brief Resolve fixed and responsive scene world viewport layouts.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game_presentation.h"

static bool scene_viewport_rect_from_array(yyjson_val *value, SDL_Rect *out_rect)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 4 || out_rect == NULL)
        return false;

    const double width = yyjson_get_num(yyjson_arr_get(value, 2));
    const double height = yyjson_get_num(yyjson_arr_get(value, 3));
    if (!(width > 0.0) || !(height > 0.0))
        return false;

    *out_rect = (SDL_Rect){
        (int)SDL_lround(yyjson_get_num(yyjson_arr_get(value, 0))),
        (int)SDL_lround(yyjson_get_num(yyjson_arr_get(value, 1))),
        (int)SDL_lround(width),
        (int)SDL_lround(height),
    };
    return true;
}

static bool resolve_legacy_viewports(const slayer3d_game_data_runtime *runtime, yyjson_val *viewports,
                                     game_data_scene_world_viewport *out_viewports, int capacity, int *out_count)
{
    int count = 0;
    for (size_t i = 0; i < yyjson_arr_size(viewports); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(viewports, i);
        yyjson_val *active_if = obj_get(entry, "active_if");
        if (!yyjson_is_obj(entry) || (active_if != NULL && !eval_data_condition(runtime, active_if, NULL)))
            continue;
        if (count >= SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX || (out_viewports != NULL && count >= capacity))
            return false;

        game_data_scene_world_viewport viewport;
        SDL_zero(viewport);
        viewport.name = json_string(entry, "name", NULL);
        viewport.camera = json_string(entry, "camera", NULL);
        viewport.label = json_string(entry, "label", NULL);
        viewport.label_font = json_string(entry, "label_font", NULL);
        viewport.draw_skybox = json_bool(entry, "skybox", true);
        viewport.draw_viewmodel = json_bool(entry, "viewmodel", false);
        viewport.label_style = obj_get(entry, "label_style");
        viewport.work_plane = obj_get(entry, "work_plane");
        viewport.grid = obj_get(entry, "grid");
        if (viewport.camera == NULL || !scene_viewport_rect_from_array(obj_get(entry, "rect"), &viewport.rect))
        {
            return false;
        }
        if (out_viewports != NULL)
            out_viewports[count] = viewport;
        ++count;
    }
    *out_count = count;
    return true;
}

static yyjson_val *find_named_entry(yyjson_val *entries, const char *name)
{
    for (size_t i = 0; yyjson_is_arr(entries) && i < yyjson_arr_size(entries); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(entries, i);
        const char *candidate = json_string(entry, "name", NULL);
        if (candidate != NULL && name != NULL && SDL_strcmp(candidate, name) == 0)
            return entry;
    }
    return NULL;
}

static float viewport_bound_value(yyjson_val *bounds, const char *key, float fallback, float remaining)
{
    yyjson_val *value = obj_get(bounds, key);
    if (yyjson_is_num(value))
        return (float)yyjson_get_num(value);
    if (yyjson_is_str(value) && SDL_strcmp(yyjson_get_str(value), "fill") == 0)
        return remaining;
    return fallback;
}

static void reserve_docked_ui(const slayer3d_game_data_runtime *runtime, float viewport_width, float viewport_height,
                              bool reserve_left, bool reserve_right, bool reserve_bottom, float *left, float *right,
                              float *bottom)
{
    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return;
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, viewport_width, viewport_height, NULL, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return;
    }

    for (int i = 0, count = slayer3d_ui_layout_node_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_resolved_node *node = slayer3d_ui_layout_resolved_node_at(layout, i);
        if (node == NULL || !node->window || node->rect.w <= 0.0f || node->rect.h <= 0.0f)
            continue;
        if (reserve_left && node->dock == SLAYER3D_UI_LAYOUT_DOCK_LEFT)
            *left = SDL_max(*left, node->rect.x + node->rect.w);
        else if (reserve_right && node->dock == SLAYER3D_UI_LAYOUT_DOCK_RIGHT)
            *right = SDL_min(*right, node->rect.x);
        else if (reserve_bottom && node->dock == SLAYER3D_UI_LAYOUT_DOCK_BOTTOM)
            *bottom = SDL_min(*bottom, node->rect.y);
    }
    slayer3d_ui_layout_destroy(layout);
}

static bool viewport_layout_reserves_dock(yyjson_val *value, const char *dock)
{
    if (yyjson_is_bool(value))
        return yyjson_get_bool(value);
    for (size_t i = 0; yyjson_is_arr(value) && i < yyjson_arr_size(value); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(value, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), dock) == 0)
            return true;
    }
    return false;
}

static bool resolve_layout_viewports(const slayer3d_game_data_runtime *runtime, yyjson_val *config,
                                     game_data_scene_world_viewport *out_viewports, int capacity, int *out_count)
{
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    slayer3d_game_data_ui_viewport(runtime, &viewport_width, &viewport_height);

    yyjson_val *bounds = obj_get(config, "bounds");
    float left = viewport_bound_value(bounds, "x", 0.0f, viewport_width);
    float top = viewport_bound_value(bounds, "y", 0.0f, viewport_height);
    float width = viewport_bound_value(bounds, "width", viewport_width - left, viewport_width - left);
    float height = viewport_bound_value(bounds, "height", viewport_height - top, viewport_height - top);
    float right = SDL_min(left + width, viewport_width);
    float bottom = SDL_min(top + height, viewport_height);
    const char *default_layout = json_string(config, "default_layout", NULL);
    const char *layout_name = default_layout;
    const char *state_key = json_string(config, "layout_key", NULL);
    if (runtime->scene_state != NULL && state_key != NULL)
        layout_name = slayer3d_properties_get_string(runtime->scene_state, state_key, default_layout);
    yyjson_val *layout = find_named_entry(obj_get(config, "layouts"), layout_name);
    if (layout == NULL)
        layout = find_named_entry(obj_get(config, "layouts"), default_layout);
    if (layout == NULL)
        return false;

    yyjson_val *reserved_docks = obj_get(layout, "avoid_docked_ui");
    if (reserved_docks == NULL)
        reserved_docks = obj_get(config, "avoid_docked_ui");
    if (viewport_layout_reserves_dock(reserved_docks, "left") ||
        viewport_layout_reserves_dock(reserved_docks, "right") ||
        viewport_layout_reserves_dock(reserved_docks, "bottom"))
    {
        reserve_docked_ui(runtime, viewport_width, viewport_height,
                          viewport_layout_reserves_dock(reserved_docks, "left"),
                          viewport_layout_reserves_dock(reserved_docks, "right"),
                          viewport_layout_reserves_dock(reserved_docks, "bottom"), &left, &right, &bottom);
    }
    if (right <= left || bottom <= top)
        return false;

    const int columns = json_int(layout, "columns", 1);
    const int rows = json_int(layout, "rows", 1);
    const float gap = json_float(config, "gap", 0.0f);
    if (columns < 1 || rows < 1 || gap < 0.0f)
        return false;

    const float usable_width = right - left - gap * (float)(columns - 1);
    const float usable_height = bottom - top - gap * (float)(rows - 1);
    if (usable_width <= 0.0f || usable_height <= 0.0f)
        return false;

    yyjson_val *views = obj_get(config, "views");
    yyjson_val *panes = obj_get(layout, "panes");
    const char *default_label_font = json_string(config, "label_font", NULL);
    yyjson_val *default_label_style = obj_get(config, "label_style");
    int count = 0;
    for (size_t i = 0; yyjson_is_arr(panes) && i < yyjson_arr_size(panes); ++i)
    {
        yyjson_val *pane = yyjson_arr_get(panes, i);
        const int column = json_int(pane, "column", 0);
        const int row = json_int(pane, "row", 0);
        const int column_span = json_int(pane, "column_span", 1);
        const int row_span = json_int(pane, "row_span", 1);
        if (column < 0 || row < 0 || column_span < 1 || row_span < 1 || column + column_span > columns ||
            row + row_span > rows || count >= SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX ||
            (out_viewports != NULL && count >= capacity))
        {
            return false;
        }

        yyjson_val *view = find_named_entry(views, json_string(pane, "view", NULL));
        const char *camera = json_string(view, "camera", NULL);
        if (view == NULL || camera == NULL)
            return false;

        const float x0 = left + (float)column * usable_width / (float)columns + (float)column * gap;
        const float y0 = top + (float)row * usable_height / (float)rows + (float)row * gap;
        const float x1 = left + (float)(column + column_span) * usable_width / (float)columns +
                         (float)(column + column_span - 1) * gap;
        const float y1 =
            top + (float)(row + row_span) * usable_height / (float)rows + (float)(row + row_span - 1) * gap;

        game_data_scene_world_viewport viewport;
        SDL_zero(viewport);
        viewport.name = json_string(view, "name", NULL);
        viewport.camera = camera;
        viewport.label = json_string(view, "label", NULL);
        viewport.label_font = json_string(view, "label_font", default_label_font);
        viewport.draw_skybox = json_bool(view, "skybox", true);
        viewport.draw_viewmodel = json_bool(view, "viewmodel", false);
        viewport.label_style = obj_get(view, "label_style");
        if (viewport.label_style == NULL)
            viewport.label_style = default_label_style;
        viewport.rect.x = (int)SDL_lroundf(x0);
        viewport.rect.y = (int)SDL_lroundf(y0);
        viewport.rect.w = SDL_max((int)SDL_lroundf(x1) - viewport.rect.x, 1);
        viewport.rect.h = SDL_max((int)SDL_lroundf(y1) - viewport.rect.y, 1);
        viewport.work_plane = obj_get(view, "work_plane");
        viewport.grid = obj_get(view, "grid");
        if (out_viewports != NULL)
            out_viewports[count] = viewport;
        ++count;
    }

    *out_count = count;
    return count > 0;
}

bool game_data_resolve_active_scene_world_viewports(const slayer3d_game_data_runtime *runtime,
                                                    game_data_scene_world_viewport *out_viewports, int capacity,
                                                    int *out_count)
{
    if (runtime == NULL || out_count == NULL || capacity < 0 || (out_viewports != NULL && capacity == 0))
        return false;
    *out_count = 0;

    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *viewports = obj_get(scene != NULL ? scene->root : NULL, "world_viewports");
    if (yyjson_is_arr(viewports))
        return resolve_legacy_viewports(runtime, viewports, out_viewports, capacity, out_count);
    if (yyjson_is_obj(viewports))
        return resolve_layout_viewports(runtime, viewports, out_viewports, capacity, out_count);
    return true;
}

bool slayer3d_game_data_resolve_active_world_viewports(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_game_data_world_viewport *out_viewports, int capacity,
                                                       int *out_count)
{
    game_data_scene_world_viewport resolved[SLAYER3D_GAME_DATA_WORLD_VIEWPORT_MAX];
    int count = 0;
    if (!game_data_resolve_active_scene_world_viewports(runtime, resolved, SDL_arraysize(resolved), &count))
        return false;
    if (out_viewports != NULL && capacity < count)
        return false;
    for (int i = 0; out_viewports != NULL && i < count; ++i)
    {
        out_viewports[i].name = resolved[i].name;
        out_viewports[i].camera = resolved[i].camera;
        out_viewports[i].label = resolved[i].label;
        out_viewports[i].rect = resolved[i].rect;
        out_viewports[i].draw_skybox = resolved[i].draw_skybox;
        out_viewports[i].draw_viewmodel = resolved[i].draw_viewmodel;
    }
    if (out_count != NULL)
        *out_count = count;
    return out_count != NULL;
}

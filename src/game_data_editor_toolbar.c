/**
 * @file game_data_editor_toolbar.c
 * @brief Editor toolbar widget handling.
 */

#include "game_data_internal.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

static bool editor_retained_ui_hit(const slayer3d_game_data_runtime *runtime, float mouse_x, float mouse_y,
                                   slayer3d_ui_layout_model **out_layout, const slayer3d_ui_layout_hit_region **out_hit)
{
    if (out_layout != NULL)
        *out_layout = NULL;
    if (out_hit != NULL)
        *out_hit = NULL;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return false;
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
    slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
    if (!slayer3d_game_data_build_active_ui_widget_layout(runtime, viewport_w, viewport_h, NULL, layout))
    {
        slayer3d_ui_layout_destroy(layout);
        return false;
    }

    const slayer3d_ui_layout_hit_region *hit = slayer3d_ui_layout_hit_test(layout, mouse_x, mouse_y);
    if (out_hit != NULL)
        *out_hit = hit;
    if (out_layout != NULL)
        *out_layout = layout;
    else
        slayer3d_ui_layout_destroy(layout);
    return hit != NULL;
}

static bool editor_hit_id_has_prefix(const slayer3d_ui_layout_hit_region *hit, const char *prefix)
{
    if (hit == NULL || prefix == NULL)
        return false;
    return SDL_strncmp(hit->id, prefix, SDL_strlen(prefix)) == 0;
}

static bool editor_hit_is_toolbar(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.toolbar.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.tool_toolbar.");
}

static bool editor_hit_is_texture_viewer(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.");
}

static bool editor_hit_is_file_menu(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.file_menu.");
}

static bool editor_hit_is_global_panel(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.");
}

static bool editor_hit_is_stair_panel(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.stair_panel.");
}

static bool editor_hit_is_liquid_panel(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.liquid_panel.");
}

static bool editor_hit_is_skybox_panel(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.skybox_panel.");
}

static bool editor_hit_is_actor_viewer(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.actor_viewer.");
}

static bool editor_hit_is_left_inspector(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.");
}

static bool editor_hit_is_console(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.console.");
}

static bool editor_hit_is_synthesized_scrollbar(const slayer3d_ui_layout_hit_region *hit)
{
    if (hit == NULL)
        return false;
    const size_t id_len = SDL_strlen(hit->id);
    const size_t suffix_len = SDL_strlen(SLAYER3D_UI_LAYOUT_SCROLLBAR_SUFFIX);
    return id_len >= suffix_len && SDL_strcmp(hit->id + id_len - suffix_len, SLAYER3D_UI_LAYOUT_SCROLLBAR_SUFFIX) == 0;
}

static bool editor_hit_is_outside_click_capture(const slayer3d_ui_layout_hit_region *hit)
{
    return hit != NULL && hit->outside_click;
}

typedef struct editor_ui_window_config
{
    const char *id;
    const char *x_key;
    const char *y_key;
    const char *width_key;
    const char *height_key;
    const char *resolved_height_key;
    const char *dock_key;
    const char *dock_width_key;
    const char *dock_height_key;
    const char *front_key;
    const char *drag_handle;
    const char *resize_handle;
    const char *resize_edge;
    float floating_width;
    float floating_height;
    float dock_top;
    float dock_bottom;
    float snap_distance;
    float drag_threshold;
    float titlebar_visible_width;
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    float min_dock_width;
    float max_dock_width;
    float min_dock_height;
    float max_dock_height;
} editor_ui_window_config;

static yyjson_val *editor_find_ui_widget_node(yyjson_val *node, const char *id)
{
    if (!yyjson_is_obj(node) || id == NULL)
        return NULL;
    const char *node_id = json_string(node, "id", NULL);
    if (node_id != NULL && SDL_strcmp(node_id, id) == 0)
        return node;

    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
    {
        yyjson_val *found = editor_find_ui_widget_node(yyjson_arr_get(children, i), id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static yyjson_val *editor_find_active_ui_widget(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *roots[2] = {runtime_root(runtime), NULL};
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;

    for (size_t root_index = 0; root_index < SDL_arraysize(roots); ++root_index)
    {
        yyjson_val *widgets = obj_get(obj_get(roots[root_index], "ui"), "widgets");
        for (size_t i = 0; yyjson_is_arr(widgets) && i < yyjson_arr_size(widgets); ++i)
        {
            yyjson_val *found = editor_find_ui_widget_node(yyjson_arr_get(widgets, i), id);
            if (found != NULL)
                return found;
        }
    }
    return NULL;
}

static yyjson_val *editor_find_open_bound_dropdown_node(const slayer3d_game_data_runtime *runtime, yyjson_val *node)
{
    if (!yyjson_is_obj(node))
        return NULL;
    const char *open_key = json_string(node, "open_key", NULL);
    if (open_key != NULL && yyjson_is_arr(obj_get(node, "values")) &&
        slayer3d_properties_get_bool(runtime->scene_state, open_key, false))
    {
        return node;
    }
    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
    {
        yyjson_val *found = editor_find_open_bound_dropdown_node(runtime, yyjson_arr_get(children, i));
        if (found != NULL)
            return found;
    }
    return NULL;
}

static yyjson_val *editor_find_open_bound_dropdown(const slayer3d_game_data_runtime *runtime)
{
    yyjson_val *roots[2] = {runtime_root(runtime), NULL};
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;
    for (size_t root_index = 0; root_index < SDL_arraysize(roots); ++root_index)
    {
        yyjson_val *widgets = obj_get(obj_get(roots[root_index], "ui"), "widgets");
        for (size_t i = 0; yyjson_is_arr(widgets) && i < yyjson_arr_size(widgets); ++i)
        {
            yyjson_val *found = editor_find_open_bound_dropdown_node(runtime, yyjson_arr_get(widgets, i));
            if (found != NULL)
                return found;
        }
    }
    return NULL;
}

static void editor_close_bound_dropdowns_node(slayer3d_properties *state, yyjson_val *node, const char *except_id)
{
    if (!yyjson_is_obj(node))
        return;
    const char *id = json_string(node, "id", NULL);
    const char *open_key = json_string(node, "open_key", NULL);
    if (open_key != NULL && (except_id == NULL || id == NULL || SDL_strcmp(id, except_id) != 0))
        slayer3d_properties_set_bool(state, open_key, false);
    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
        editor_close_bound_dropdowns_node(state, yyjson_arr_get(children, i), except_id);
}

static void editor_close_bound_dropdowns(const slayer3d_game_data_runtime *runtime, const char *except_id)
{
    yyjson_val *roots[2] = {runtime_root(runtime), NULL};
    const scene_entry *scene = active_scene_entry_const(runtime);
    roots[1] = scene != NULL ? scene->root : NULL;
    for (size_t root_index = 0; root_index < SDL_arraysize(roots); ++root_index)
    {
        yyjson_val *widgets = obj_get(obj_get(roots[root_index], "ui"), "widgets");
        for (size_t i = 0; yyjson_is_arr(widgets) && i < yyjson_arr_size(widgets); ++i)
            editor_close_bound_dropdowns_node(runtime->scene_state, yyjson_arr_get(widgets, i), except_id);
    }
}

static bool editor_set_bound_dropdown_value(slayer3d_properties *state, yyjson_val *widget, int option_index)
{
    const char *key = json_string(widget, "selected_value_key", NULL);
    yyjson_val *values = obj_get(widget, "values");
    if (state == NULL || key == NULL || !yyjson_is_arr(values) || option_index < 0 ||
        option_index >= (int)yyjson_arr_size(values))
    {
        return false;
    }
    yyjson_val *value = yyjson_arr_get(values, (size_t)option_index);
    if (yyjson_is_str(value))
        slayer3d_properties_set_string(state, key, yyjson_get_str(value));
    else if (yyjson_is_bool(value))
        slayer3d_properties_set_bool(state, key, yyjson_get_bool(value));
    else if (yyjson_is_int(value))
        slayer3d_properties_set_int(state, key, (int)yyjson_get_sint(value));
    else if (yyjson_is_num(value))
        slayer3d_properties_set_float(state, key, (float)yyjson_get_num(value));
    else
        return false;
    return true;
}

static bool editor_handle_bound_dropdown(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_hit_region *hit,
                                         bool clicked)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    yyjson_val *open_widget = editor_find_open_bound_dropdown(runtime);
    yyjson_val *hit_widget = NULL;
    if (hit != NULL)
    {
        hit_widget = editor_find_active_ui_widget(runtime, hit->owner_id);
        if (hit_widget == NULL)
            hit_widget = editor_find_active_ui_widget(runtime, hit->id);
    }
    const char *open_key = json_string(hit_widget, "open_key", NULL);
    const char *widget_id = json_string(hit_widget, "id", NULL);
    if (open_key == NULL || !yyjson_is_arr(obj_get(hit_widget, "values")))
    {
        if (clicked && open_widget != NULL)
        {
            slayer3d_properties_set_bool(runtime->scene_state, json_string(open_widget, "open_key", ""), false);
            return true;
        }
        return false;
    }

    if (!clicked)
        return true;
    if (hit->option_index >= 0)
    {
        (void)editor_set_bound_dropdown_value(runtime->scene_state, hit_widget, hit->option_index);
        const char *selected_key = json_string(hit_widget, "selected_value_key", NULL);
        if (selected_key != NULL && SDL_strcmp(selected_key, "editor.view.layout") == 0)
        {
            yyjson_val *options = obj_get(hit_widget, "options");
            const char *label =
                yyjson_is_arr(options) ? yyjson_get_str(yyjson_arr_get(options, (size_t)hit->option_index)) : NULL;
            char message[128];
            SDL_snprintf(message, sizeof(message), "View layout selected: %s (%d pane%s)",
                         label != NULL ? label : "custom", hit->option_index + 1, hit->option_index == 0 ? "" : "s");
            editor_publish_console_message(runtime, message);
        }
        slayer3d_properties_set_bool(runtime->scene_state, open_key, false);
        return true;
    }

    const bool next_open = !slayer3d_properties_get_bool(runtime->scene_state, open_key, false);
    editor_close_bound_dropdowns(runtime, widget_id);
    slayer3d_properties_set_bool(runtime->scene_state, open_key, next_open);
    return true;
}

static bool editor_ui_window_config_for_id(const slayer3d_game_data_runtime *runtime, const char *id,
                                           editor_ui_window_config *out_config)
{
    if (out_config == NULL)
        return false;
    SDL_zero(*out_config);
    yyjson_val *node = editor_find_active_ui_widget(runtime, id);
    yyjson_val *window = obj_get(node, "window");
    if (!yyjson_is_obj(window))
        return false;

    out_config->id = json_string(node, "id", NULL);
    out_config->x_key = json_string(node, "x_key", NULL);
    out_config->y_key = json_string(node, "y_key", NULL);
    out_config->width_key = json_string(node, "w_key", NULL);
    out_config->height_key = json_string(window, "height_key", NULL);
    out_config->resolved_height_key = json_string(window, "resolved_height_key", NULL);
    out_config->dock_key = json_string(window, "dock_key", NULL);
    out_config->dock_width_key = json_string(window, "dock_width_key", NULL);
    out_config->dock_height_key = json_string(window, "dock_height_key", NULL);
    out_config->front_key = json_string(window, "front_key", NULL);
    out_config->drag_handle = json_string(window, "drag_handle", NULL);
    out_config->resize_handle = json_string(window, "resize_handle", NULL);
    out_config->resize_edge = json_string(window, "resize_edge", NULL);
    out_config->floating_width = json_float(node, "w", 0.0f);
    out_config->floating_height = json_float(node, "h", 0.0f);
    const char *width_key = json_string(node, "w_key", NULL);
    const char *height_key = json_string(node, "h_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && width_key != NULL)
    {
        out_config->floating_width =
            slayer3d_properties_get_float(runtime->scene_state, width_key, out_config->floating_width);
    }
    if (runtime != NULL && runtime->scene_state != NULL && height_key != NULL)
    {
        out_config->floating_height =
            slayer3d_properties_get_float(runtime->scene_state, height_key, out_config->floating_height);
    }
    out_config->dock_top = json_float(window, "dock_top", 0.0f);
    out_config->dock_bottom = json_float(window, "dock_bottom", 0.0f);
    const char *dock_bottom_key = json_string(window, "dock_bottom_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && dock_bottom_key != NULL)
    {
        out_config->dock_bottom =
            slayer3d_properties_get_float(runtime->scene_state, dock_bottom_key, out_config->dock_bottom);
    }
    out_config->snap_distance = json_float(window, "snap_distance", 48.0f);
    out_config->drag_threshold = json_float(window, "drag_threshold", 4.0f);
    out_config->titlebar_visible_width = json_float(window, "titlebar_visible_width", 48.0f);
    out_config->min_width = json_float(window, "min_width", 160.0f);
    out_config->max_width = json_float(window, "max_width", 0.0f);
    out_config->min_height = json_float(window, "min_height", 72.0f);
    out_config->max_height = json_float(window, "max_height", 360.0f);
    out_config->min_dock_width = json_float(window, "min_dock_width", 0.0f);
    out_config->max_dock_width = json_float(window, "max_dock_width", 0.0f);
    out_config->min_dock_height = json_float(window, "min_dock_height", 0.0f);
    out_config->max_dock_height = json_float(window, "max_dock_height", 0.0f);
    return out_config->id != NULL;
}

static const slayer3d_ui_layout_resolved_node *editor_resolved_window_for_hit(const slayer3d_ui_layout_model *layout,
                                                                              const slayer3d_ui_layout_hit_region *hit)
{
    if (layout == NULL || hit == NULL)
        return NULL;
    const slayer3d_ui_layout_resolved_node *node = slayer3d_ui_layout_find_resolved_node(layout, hit->owner_id);
    while (node != NULL)
    {
        if (node->window)
            return node;
        node = node->parent_index >= 0 ? slayer3d_ui_layout_resolved_node_at(layout, node->parent_index) : NULL;
    }
    return NULL;
}

static void editor_end_ui_window_pointer_capture(slayer3d_properties *scene_state)
{
    if (scene_state == NULL)
        return;
    slayer3d_properties_set_string(scene_state, "editor.ui.window.pointer.id", "");
    slayer3d_properties_set_string(scene_state, "editor.ui.window.pointer.mode", "");
    slayer3d_properties_set_string(scene_state, "editor.ui.window.pointer.preview", "none");
    slayer3d_properties_set_int(scene_state, "editor.ui.window.pointer.resize_edge",
                                SLAYER3D_UI_LAYOUT_RESIZE_EDGE_NONE);
}

static const char *editor_ui_window_dock_preview(const editor_ui_window_config *config, float mouse_x, float mouse_y,
                                                 slayer3d_ui_layout_rect viewport)
{
    if (config == NULL)
        return "none";
    if (config->dock_key == NULL)
        return "none";

    const float left_distance = mouse_x - viewport.x;
    const float right_distance = viewport.x + viewport.w - mouse_x;
    const float bottom_distance = viewport.y + viewport.h - mouse_y;
    float closest = config->snap_distance + 1.0f;
    const char *preview = "none";
    if (left_distance <= config->snap_distance && left_distance < closest)
    {
        closest = left_distance;
        preview = "left";
    }
    if (right_distance <= config->snap_distance && right_distance < closest)
    {
        closest = right_distance;
        preview = "right";
    }
    if (bottom_distance <= config->snap_distance && bottom_distance < closest)
        preview = "bottom";
    return preview;
}

static slayer3d_ui_layout_dock editor_ui_window_dock_from_string(const char *dock)
{
    if (dock != NULL && SDL_strcmp(dock, "left") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_LEFT;
    if (dock != NULL && SDL_strcmp(dock, "right") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_RIGHT;
    if (dock != NULL && SDL_strcmp(dock, "bottom") == 0)
        return SLAYER3D_UI_LAYOUT_DOCK_BOTTOM;
    return SLAYER3D_UI_LAYOUT_DOCK_NONE;
}

static void editor_update_ui_window_preview(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                            const editor_ui_window_config *config, float floating_x, float floating_y,
                                            float floating_w, float floating_h, float mouse_x, float mouse_y,
                                            slayer3d_ui_layout_rect viewport)
{
    if (runtime == NULL || runtime->scene_state == NULL || config == NULL)
        return;

    slayer3d_properties *state = runtime->scene_state;
    const char *preview = editor_ui_window_dock_preview(config, mouse_x, mouse_y, viewport);
    slayer3d_ui_layout_rect preview_rect = {floating_x, floating_y, floating_w, floating_h};
    const slayer3d_ui_layout_dock dock = editor_ui_window_dock_from_string(preview);
    if (dock != SLAYER3D_UI_LAYOUT_DOCK_NONE)
    {
        if (slayer3d_ui_layout_calculate_window_dock_rect_in_rect(layout, config->id, dock, viewport, &preview_rect))
        {
            preview_rect.x -= viewport.x;
            preview_rect.y -= viewport.y;
        }
    }

    slayer3d_properties_set_string(state, "editor.ui.window.pointer.preview", preview);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.x", preview_rect.x);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.y", preview_rect.y);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.w", preview_rect.w);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.h", preview_rect.h);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.x", 0.0f);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.y", config->dock_top);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.w", viewport.w);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.h",
                                  SDL_max(viewport.h - config->dock_top - config->dock_bottom, 0.0f));
}

static void editor_clamp_ui_window_to_titlebar(const slayer3d_ui_layout_model *layout,
                                               const editor_ui_window_config *config,
                                               const slayer3d_ui_layout_resolved_node *window, float viewport_w,
                                               float viewport_h, float *in_out_x, float *in_out_y)
{
    if (layout == NULL || config == NULL || window == NULL || in_out_x == NULL || in_out_y == NULL)
        return;

    const slayer3d_ui_layout_resolved_node *titlebar =
        config->drag_handle != NULL ? slayer3d_ui_layout_find_resolved_node(layout, config->drag_handle) : NULL;
    if (titlebar == NULL)
        return;

    const float local_x = titlebar->rect.x - window->rect.x;
    const float local_y = titlebar->rect.y - window->rect.y;
    const float visible_width = SDL_min(config->titlebar_visible_width, titlebar->rect.w);
    const float min_x = visible_width - local_x - titlebar->rect.w;
    const float max_x = viewport_w - visible_width - local_x;
    const float min_y = -local_y;
    const float max_y = viewport_h - local_y - titlebar->rect.h;
    *in_out_x = editor_clamp_float(*in_out_x, min_x, SDL_max(max_x, min_x));
    *in_out_y = editor_clamp_float(*in_out_y, min_y, SDL_max(max_y, min_y));
}

static void editor_sync_ui_window_resolved_state(slayer3d_game_data_runtime *runtime,
                                                 const slayer3d_ui_layout_model *layout)
{
    if (runtime == NULL || runtime->scene_state == NULL || layout == NULL)
        return;

    bool refresh_console = false;
    const int node_count = slayer3d_ui_layout_node_count(layout);
    for (int i = 0; i < node_count; ++i)
    {
        const slayer3d_ui_layout_resolved_node *node = slayer3d_ui_layout_resolved_node_at(layout, i);
        if (node == NULL || !node->window)
            continue;
        editor_ui_window_config config;
        if (!editor_ui_window_config_for_id(runtime, node->id, &config) || config.resolved_height_key == NULL)
            continue;
        const float prior =
            slayer3d_properties_get_float(runtime->scene_state, config.resolved_height_key, node->rect.h);
        if (SDL_fabsf(prior - node->rect.h) < 0.01f)
            continue;
        slayer3d_properties_set_float(runtime->scene_state, config.resolved_height_key, node->rect.h);
        refresh_console =
            refresh_console || SDL_strcmp(config.resolved_height_key, "editor.console.visible_height") == 0;
    }
    if (refresh_console)
        editor_refresh_console_lines(runtime);
}

/*
 * Root windows own their pointer capture. The layout resolves placement and
 * stacking; this controller only writes the authored geometry and dock keys.
 */
static bool editor_handle_ui_window_pointer(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                            const slayer3d_ui_layout_hit_region *hit, float mouse_x, float mouse_y,
                                            bool clicked, bool left_down, bool released)
{
    if (runtime == NULL || runtime->scene_state == NULL || layout == NULL)
        return false;

    slayer3d_properties *state = runtime->scene_state;
    const char *active_id = slayer3d_properties_get_string(state, "editor.ui.window.pointer.id", "");
    const char *mode = slayer3d_properties_get_string(state, "editor.ui.window.pointer.mode", "");
    if (active_id[0] != '\0')
    {
        editor_ui_window_config config;
        const slayer3d_ui_layout_resolved_node *window = slayer3d_ui_layout_find_resolved_node(layout, active_id);
        if (!editor_ui_window_config_for_id(runtime, active_id, &config) || window == NULL)
        {
            editor_end_ui_window_pointer_capture(state);
            return true;
        }

        if (left_down)
        {
            bool dragging = SDL_strcmp(mode, "drag") == 0;
            if (SDL_strcmp(mode, "drag_pending") == 0)
            {
                const float start_x = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_x", mouse_x);
                const float start_y = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_y", mouse_y);
                const float dx = mouse_x - start_x;
                const float dy = mouse_y - start_y;
                if (dx * dx + dy * dy < config.drag_threshold * config.drag_threshold)
                    return true;
                slayer3d_properties_set_string(state, "editor.ui.window.pointer.mode", "drag");
                if (config.dock_key != NULL)
                    slayer3d_properties_set_string(state, config.dock_key, "none");
                dragging = true;
            }

            if (dragging && config.x_key != NULL && config.y_key != NULL)
            {
                const float start_x = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_x", mouse_x);
                const float start_y = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_y", mouse_y);
                slayer3d_ui_layout_rect viewport;
                slayer3d_game_data_ui_viewport_rect(runtime, &viewport);
                const float origin_x = slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_x",
                                                                     window->rect.x - viewport.x);
                const float origin_y = slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_y",
                                                                     window->rect.y - viewport.y);
                const float floating_w = config.floating_width > 0.0f ? config.floating_width : window->rect.w;
                const float floating_h = config.floating_height > 0.0f ? config.floating_height : window->rect.h;
                const float x = origin_x + mouse_x - start_x;
                const float y = origin_y + mouse_y - start_y;
                slayer3d_properties_set_float(state, config.x_key, x);
                slayer3d_properties_set_float(state, config.y_key, y);
                editor_update_ui_window_preview(runtime, layout, &config, x, y, floating_w, floating_h, mouse_x,
                                                mouse_y, viewport);
            }
            else if (SDL_strcmp(mode, "resize") == 0 && config.height_key != NULL && config.resize_edge != NULL &&
                     SDL_strcmp(config.resize_edge, "top") == 0)
            {
                const float bottom = window->rect.y + window->rect.h;
                const float height = editor_clamp_float(bottom - mouse_y, config.min_height, config.max_height);
                slayer3d_properties_set_float(state, config.height_key, height);
                if (window->dock == SLAYER3D_UI_LAYOUT_DOCK_NONE && config.y_key != NULL)
                    slayer3d_properties_set_float(state, config.y_key, bottom - height);
                if (config.resolved_height_key != NULL)
                    slayer3d_properties_set_float(state, config.resolved_height_key, height);
                editor_refresh_console_lines(runtime);
            }
            else if (SDL_strcmp(mode, "floating_resize") == 0 && config.width_key != NULL && config.height_key != NULL)
            {
                slayer3d_ui_layout_rect viewport;
                slayer3d_game_data_ui_viewport_rect(runtime, &viewport);
                const float start_x = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_x", mouse_x);
                const float start_y = slayer3d_properties_get_float(state, "editor.ui.window.pointer.start_y", mouse_y);
                const float origin_width =
                    slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_width", window->rect.w);
                const float origin_height =
                    slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_height", window->rect.h);
                const float maximum_width = config.max_width > 0.0f ? config.max_width : viewport.w;
                const float maximum_height = config.max_height > 0.0f ? config.max_height : viewport.h;
                const float width =
                    editor_clamp_float(origin_width + mouse_x - start_x, config.min_width, maximum_width);
                const float height =
                    editor_clamp_float(origin_height + mouse_y - start_y, config.min_height, maximum_height);
                slayer3d_properties_set_float(state, config.width_key, width);
                slayer3d_properties_set_float(state, config.height_key, height);
            }
            else if (SDL_strcmp(mode, "dock_resize") == 0)
            {
                slayer3d_ui_layout_rect viewport;
                slayer3d_game_data_ui_viewport_rect(runtime, &viewport);
                const slayer3d_ui_layout_resize_edge edge = (slayer3d_ui_layout_resize_edge)slayer3d_properties_get_int(
                    state, "editor.ui.window.pointer.resize_edge", SLAYER3D_UI_LAYOUT_RESIZE_EDGE_NONE);
                if (edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_RIGHT && config.dock_width_key != NULL)
                {
                    const float maximum = config.max_dock_width > 0.0f ? config.max_dock_width : viewport.w;
                    const float width = editor_clamp_float(mouse_x - window->rect.x, config.min_dock_width, maximum);
                    slayer3d_properties_set_float(state, config.dock_width_key, width);
                }
                else if (edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_LEFT && config.dock_width_key != NULL)
                {
                    const float maximum = config.max_dock_width > 0.0f ? config.max_dock_width : viewport.w;
                    const float right = window->rect.x + window->rect.w;
                    const float width = editor_clamp_float(right - mouse_x, config.min_dock_width, maximum);
                    slayer3d_properties_set_float(state, config.dock_width_key, width);
                }
                else if (edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_TOP && config.dock_height_key != NULL)
                {
                    const float maximum = config.max_dock_height > 0.0f ? config.max_dock_height : viewport.h;
                    const float bottom = window->rect.y + window->rect.h;
                    const float height = editor_clamp_float(bottom - mouse_y, config.min_dock_height, maximum);
                    slayer3d_properties_set_float(state, config.dock_height_key, height);
                    if (config.resolved_height_key != NULL)
                    {
                        slayer3d_properties_set_float(state, config.resolved_height_key, height);
                        if (SDL_strcmp(config.resolved_height_key, "editor.console.visible_height") == 0)
                            editor_refresh_console_lines(runtime);
                    }
                }
            }
            return true;
        }

        if (released && SDL_strcmp(mode, "drag") == 0)
        {
            const char *preview = slayer3d_properties_get_string(state, "editor.ui.window.pointer.preview", "none");
            const char *dock = SDL_strcmp(preview, "left") == 0     ? "left"
                               : SDL_strcmp(preview, "right") == 0  ? "right"
                               : SDL_strcmp(preview, "bottom") == 0 ? "bottom"
                                                                    : "none";
            if (config.dock_key != NULL)
                slayer3d_properties_set_string(state, config.dock_key, dock);
            if (SDL_strcmp(dock, "none") == 0 && config.x_key != NULL && config.y_key != NULL)
            {
                slayer3d_ui_layout_rect viewport;
                slayer3d_game_data_ui_viewport_rect(runtime, &viewport);
                float x = slayer3d_properties_get_float(state, config.x_key, window->rect.x - viewport.x);
                float y = slayer3d_properties_get_float(state, config.y_key, window->rect.y - viewport.y);
                editor_clamp_ui_window_to_titlebar(layout, &config, window, viewport.w, viewport.h, &x, &y);
                slayer3d_properties_set_float(state, config.x_key, x);
                slayer3d_properties_set_float(state, config.y_key, y);
            }
        }
        editor_end_ui_window_pointer_capture(state);
        return true;
    }

    const slayer3d_ui_layout_resolved_node *window = editor_resolved_window_for_hit(layout, hit);
    if (!clicked || window == NULL)
        return false;

    editor_ui_window_config config;
    if (!editor_ui_window_config_for_id(runtime, window->id, &config))
        return false;
    if (config.front_key != NULL)
        slayer3d_properties_set_string(state, config.front_key, window->id);
    const bool starts_drag = hit->drag_handle;
    const bool starts_dock_resize =
        hit->resize_edge != SLAYER3D_UI_LAYOUT_RESIZE_EDGE_NONE && window->dock != SLAYER3D_UI_LAYOUT_DOCK_NONE;
    const bool starts_floating_resize =
        window->dock == SLAYER3D_UI_LAYOUT_DOCK_NONE &&
        ((hit->resize_edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_BOTTOM_RIGHT && window->floating_resizable) ||
         (config.resize_handle != NULL && SDL_strcmp(hit->id, config.resize_handle) == 0));
    if (!starts_drag && !starts_dock_resize && !starts_floating_resize)
        return false;

    slayer3d_properties_set_string(state, "editor.ui.window.pointer.id", window->id);
    slayer3d_properties_set_string(state, "editor.ui.window.pointer.mode",
                                   starts_drag                                                       ? "drag_pending"
                                   : starts_dock_resize                                              ? "dock_resize"
                                   : hit->resize_edge == SLAYER3D_UI_LAYOUT_RESIZE_EDGE_BOTTOM_RIGHT ? "floating_resize"
                                                                                                     : "resize");
    slayer3d_properties_set_int(state, "editor.ui.window.pointer.resize_edge",
                                starts_dock_resize ? (int)hit->resize_edge : (int)SLAYER3D_UI_LAYOUT_RESIZE_EDGE_NONE);
    if (starts_drag || starts_floating_resize)
    {
        slayer3d_ui_layout_rect viewport;
        slayer3d_game_data_ui_viewport_rect(runtime, &viewport);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.start_x", mouse_x);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.start_y", mouse_y);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_x", window->rect.x - viewport.x);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_y", window->rect.y - viewport.y);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_width", window->rect.w);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_height", window->rect.h);
        slayer3d_properties_set_string(state, "editor.ui.window.pointer.preview", "none");
    }
    return true;
}

static bool editor_hit_is_property_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.property.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.row.property");
}

static bool editor_hit_is_texture_edit_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.search.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.texture_viewer.path.");
}

static bool editor_hit_is_global_edit_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.key.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.value.") ||
           editor_hit_id_has_prefix(hit, "ui.editor_shell.global_panel.data.row");
}

static bool editor_hit_is_settings_edit_control(const slayer3d_ui_layout_hit_region *hit)
{
    return editor_hit_id_has_prefix(hit, "ui.editor_shell.settings.field.");
}

static bool editor_focus_settings_field(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_hit_region *hit)
{
    if (runtime == NULL || runtime->scene_state == NULL || hit == NULL)
        return false;
    const char *focus = NULL;
    if (SDL_strcmp(hit->id, "ui.editor_shell.settings.field.media") == 0)
        focus = "media";
    else if (SDL_strcmp(hit->id, "ui.editor_shell.settings.field.runner") == 0)
        focus = "runner";
    else if (SDL_strcmp(hit->id, "ui.editor_shell.settings.field.arguments") == 0)
        focus = "arguments";
    if (focus == NULL)
        return false;
    slayer3d_properties_set_string(runtime->scene_state, "editor.settings.edit.focus", focus);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.settings.edit.replace_on_text", true);
    editor_update_settings_edit_display(runtime);
    return true;
}

static const char *editor_property_row_select_action(const slayer3d_ui_layout_hit_region *hit, char *buffer,
                                                     size_t buffer_size)
{
    if (hit == NULL || buffer == NULL || buffer_size == 0U || hit->action[0] != '\0' ||
        !editor_hit_id_has_prefix(hit, "ui.editor_shell.left_inspector.row.property") ||
        SDL_strstr(hit->id, ".delete") != NULL)
    {
        return hit != NULL ? hit->action : "";
    }
    const char *slot_text = hit->id + SDL_strlen("ui.editor_shell.left_inspector.row.property");
    if (slot_text[0] < '0' || slot_text[0] > '9')
        return "";
    const int slot = slot_text[0] - '0';
    SDL_snprintf(buffer, buffer_size, "editor.property.select_slot.%d", slot);
    return buffer;
}

const slayer3d_ui_layout_hit_region *editor_find_layout_hit_by_id(const slayer3d_ui_layout_model *layout,
                                                                  const char *id)
{
    if (layout == NULL || id == NULL)
        return NULL;

    for (int i = 0, count = slayer3d_ui_layout_hit_region_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_hit_region *candidate = slayer3d_ui_layout_hit_region_at(layout, i);
        if (candidate != NULL && SDL_strcmp(candidate->id, id) == 0)
            return candidate;
    }
    return NULL;
}

const slayer3d_ui_layout_render_command *editor_find_layout_render_by_id(const slayer3d_ui_layout_model *layout,
                                                                         const char *id)
{
    if (layout == NULL || id == NULL)
        return NULL;

    for (int i = 0, count = slayer3d_ui_layout_render_command_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_render_command *candidate = slayer3d_ui_layout_render_command_at(layout, i);
        if (candidate != NULL && SDL_strcmp(candidate->id, id) == 0)
            return candidate;
    }
    return NULL;
}

float editor_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

/*
 * Write a scrollable pane's owning state key in its authored type and emit
 * the pane's rebind signal when the offset changed. Shared by scrollbar
 * drags and wheel scrolling so every pane behaves identically.
 */
static bool editor_write_scroll_key(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_resolved_node *pane,
                                    float scroll, const char *last_action)
{
    if (runtime == NULL || runtime->scene_state == NULL || pane == NULL || pane->scroll_key[0] == '\0')
        return false;

    const slayer3d_value *existing = slayer3d_properties_get_value(runtime->scene_state, pane->scroll_key);
    const bool integer_key = pane->scroll_virtual || (existing != NULL && existing->type == SLAYER3D_VALUE_INT);
    bool changed = false;
    if (integer_key)
    {
        const int rounded = (int)SDL_lroundf(scroll);
        changed = slayer3d_properties_get_int(runtime->scene_state, pane->scroll_key, 0) != rounded;
        slayer3d_properties_set_int(runtime->scene_state, pane->scroll_key, rounded);
    }
    else
    {
        changed = slayer3d_properties_get_float(runtime->scene_state, pane->scroll_key, 0.0f) != scroll;
        slayer3d_properties_set_float(runtime->scene_state, pane->scroll_key, scroll);
    }
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", last_action);
    if (changed && pane->scroll_signal[0] != '\0')
        (void)editor_emit_signal_by_name(runtime, pane->scroll_signal);
    return true;
}

/*
 * One drag handler for every synthesized scrollbar. The layout owns thumb
 * and travel math for pixel panes and virtualized lists alike; this just
 * maps the pointer through the pane's resolved geometry.
 */
static bool editor_update_scrollbar_drag(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                         const char *pane_id, float mouse_y)
{
    if (runtime == NULL || layout == NULL || pane_id == NULL || pane_id[0] == '\0')
        return false;

    const slayer3d_ui_layout_resolved_node *pane = slayer3d_ui_layout_find_resolved_node(layout, pane_id);
    float scroll = 0.0f;
    if (pane == NULL || !slayer3d_ui_layout_scrollbar_offset_for_pointer(layout, pane_id, mouse_y, &scroll))
        return false;
    return editor_write_scroll_key(runtime, pane, scroll, "scrollbar dragged");
}

/* Front-most scrollable pane (pixel or virtualized) under the pointer. */
static const slayer3d_ui_layout_resolved_node *editor_scrollable_pane_at(const slayer3d_ui_layout_model *layout,
                                                                         float x, float y)
{
    const slayer3d_ui_layout_resolved_node *best = NULL;
    for (int i = 0, count = slayer3d_ui_layout_node_count(layout); i < count; ++i)
    {
        const slayer3d_ui_layout_resolved_node *node = slayer3d_ui_layout_resolved_node_at(layout, i);
        if (node == NULL || node->scroll_key[0] == '\0' ||
            (node->type != SLAYER3D_UI_LAYOUT_NODE_SCROLL && !node->scroll_virtual))
            continue;
        if (x < node->rect.x || x >= node->rect.x + node->rect.w || y < node->rect.y ||
            y >= node->rect.y + node->rect.h)
            continue;
        if (node->has_clip_rect && (x < node->clip_rect.x || x >= node->clip_rect.x + node->clip_rect.w ||
                                    y < node->clip_rect.y || y >= node->clip_rect.y + node->clip_rect.h))
            continue;
        if (best == NULL || node->layer >= best->layer)
            best = node;
    }
    return best;
}

/*
 * Route wheel input to the scrollable pane under the pointer: one notch is
 * one item for virtualized lists and a fixed pixel step for scroll panes.
 * Consuming the event here keeps panes from leaking wheel motion into
 * camera zoom or the world viewport.
 */
static bool editor_update_wheel_scroll(slayer3d_game_data_runtime *runtime, const slayer3d_ui_layout_model *layout,
                                       float mouse_x, float mouse_y, float wheel_y)
{
    if (runtime == NULL || runtime->scene_state == NULL || layout == NULL || wheel_y == 0.0f)
        return false;

    const slayer3d_ui_layout_resolved_node *pane = editor_scrollable_pane_at(layout, mouse_x, mouse_y);
    if (pane == NULL)
        return false;

    const float default_step = pane->scroll_virtual ? 1.0f : 40.0f;
    const float step = pane->scroll_step > 0.0f ? pane->scroll_step : default_step;
    const float current = pane->scroll_offset;
    const float target = SDL_clamp(current - wheel_y * step, 0.0f, pane->scroll_max);
    return editor_write_scroll_key(runtime, pane, target, "pane scrolled");
}

static bool editor_actor_select_action(const char *action)
{
    static const char *prefix = "editor.actor.select.";
    static const char *slot_prefix = "editor.actor.select_slot.";
    return action != NULL &&
           ((SDL_strncmp(action, prefix, SDL_strlen(prefix)) == 0 && action[SDL_strlen(prefix)] != '\0') ||
            (SDL_strncmp(action, slot_prefix, SDL_strlen(slot_prefix)) == 0 &&
             action[SDL_strlen(slot_prefix)] != '\0'));
}

static bool editor_actor_placement_mode(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;
    const char *mode = slayer3d_properties_get_string(runtime->scene_state, "editor.tool.mode", "");
    return mode != NULL && SDL_strncmp(mode, "actor_", 6) == 0;
}

static bool editor_texture_select_action_is_blocked(slayer3d_game_data_runtime *runtime, const char *action)
{
    static const char *prefix = "editor.texture.select.";
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL ||
        SDL_strncmp(action, prefix, SDL_strlen(prefix)) != 0)
    {
        return false;
    }

    const char *texture_id = action + SDL_strlen(prefix);
    if (texture_id[0] == '\0')
        return false;

    char pending_key[160];
    char failed_key[160];
    SDL_snprintf(pending_key, sizeof(pending_key), "asset_warmup.ui_image.image.editor_shell.texture.%s.pending",
                 texture_id);
    SDL_snprintf(failed_key, sizeof(failed_key), "asset_warmup.ui_image.image.editor_shell.texture.%s.failed",
                 texture_id);

    if (slayer3d_properties_get_bool(runtime->scene_state, pending_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture loading");
        return true;
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, failed_key, false))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", "texture unavailable");
        return true;
    }
    return false;
}

typedef struct editor_tool_mode_def
{
    const char *mode;
    const char *default_message;
    const char *no_selection_message; /* used instead when nothing is selected */
} editor_tool_mode_def;

static const editor_tool_mode_def editor_tool_mode_defs[] = {
    {"select", "Selection tool selected", NULL},
    {"brush", "Brush tool selected", NULL},
    {"face", "Face tool selected", NULL},
    {"edge", "Edge tool selected", "Edge tool requires a selected brush"},
    {"clip", "", NULL},
    {"vertex", "Vertex tool selected", "Vertex tool requires a selected brush"},
    {"rotate", "Rotate tool selected", "Rotate tool requires a selected brush"},
    {"scale", "Scale tool selected", "Scale tool requires a selected brush"},
    {"shear", "Shear tool selected", "Shear tool requires a selected brush"},
    {"liquid", "Liquid tool selected", NULL},
};

static const editor_tool_mode_def *editor_find_tool_mode_def(const char *mode)
{
    if (mode == NULL || mode[0] == '\0')
        return NULL;
    for (size_t i = 0; i < SDL_arraysize(editor_tool_mode_defs); ++i)
    {
        if (SDL_strcmp(editor_tool_mode_defs[i].mode, mode) == 0)
            return &editor_tool_mode_defs[i];
    }
    return NULL;
}

static const char *editor_mode_for_tool_action(const char *action)
{
    static const char *prefix = "editor.tool.";
    if (action == NULL || SDL_strncmp(action, prefix, SDL_strlen(prefix)) != 0)
        return NULL;
    const editor_tool_mode_def *def = editor_find_tool_mode_def(action + SDL_strlen(prefix));
    return def != NULL ? def->mode : NULL;
}

bool slayer3d_game_data_set_editor_tool_mode(slayer3d_game_data_runtime *runtime, const char *mode,
                                             const char *message_override)
{
    if (runtime == NULL || runtime->scene_state == NULL)
        return false;

    const editor_tool_mode_def *def = editor_find_tool_mode_def(mode);
    if (def == NULL)
        return false;

    const char *message = message_override;
    if (message == NULL || message[0] == '\0')
    {
        message = def->default_message;
        if (def->no_selection_message != NULL &&
            slayer3d_properties_get_int(runtime->scene_state, "editor.selection.count", 0) <= 0)
        {
            message = def->no_selection_message;
        }
    }

    const bool entering_clip = SDL_strcmp(mode, "clip") == 0;
    const bool leaving_clip =
        SDL_strcmp(slayer3d_properties_get_string(runtime->scene_state, "editor.mode", ""), "clip") == 0 &&
        !entering_clip;

    if (SDL_strcmp(mode, "vertex") != 0)
        (void)slayer3d_game_data_clear_editor_vertex_selection(runtime);
    if (SDL_strcmp(mode, "edge") != 0)
        (void)slayer3d_game_data_clear_editor_edge_selection(runtime);
    if (leaving_clip)
        reset_editor_clip_tool_state(runtime, "");
    (void)editor_cancel_pending_brush_preview(runtime, "brush preview cancelled");
    slayer3d_properties_set_string(runtime->scene_state, "editor.mode", mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.mode", mode);
    slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action", message);
    slayer3d_properties_set_string(runtime->scene_state, "editor.palette.active", "");
    slayer3d_properties_set_bool(runtime->scene_state, "editor.texture.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.viewer.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.file.menu.open", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.global.panel.open", false);
    if (SDL_strcmp(mode, "liquid") != 0)
        slayer3d_properties_set_bool(runtime->scene_state, "editor.liquid.panel.open", false);
    else
        slayer3d_properties_set_bool(runtime->scene_state, "editor.liquid.panel.open", true);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.grid.menu.open", false);
    slayer3d_properties_set_bool(runtime->scene_state, "editor.shape.menu.open", false);
    clear_editor_command_preview(runtime);
    if (entering_clip)
        return slayer3d_game_data_enter_editor_clip_tool(runtime, message);
    if (SDL_strcmp(mode, "rotate") == 0)
        reset_editor_rotate_tool_state(runtime, message);
    if (SDL_strcmp(mode, "scale") == 0)
        reset_editor_scale_tool_state(runtime, message);
    if (SDL_strcmp(mode, "shear") == 0)
        reset_editor_shear_tool_state(runtime, message);
    if (message != NULL && message[0] != '\0')
        editor_publish_console_message(runtime, message);
    return true;
}

static bool editor_apply_widget_action(slayer3d_game_data_runtime *runtime, const char *action)
{
    if (runtime == NULL || runtime->scene_state == NULL || action == NULL || action[0] == '\0')
        return false;

    const char *mode = editor_mode_for_tool_action(action);
    if (mode != NULL)
        return slayer3d_game_data_set_editor_tool_mode(runtime, mode, NULL);

    char previous_action[512];
    char previous_console_line[512];
    SDL_strlcpy(previous_action, slayer3d_properties_get_string(runtime->scene_state, "editor.tool.last_action", ""),
                sizeof(previous_action));
    SDL_strlcpy(previous_console_line,
                slayer3d_properties_get_string(runtime->scene_state, "editor.console.history0", ""),
                sizeof(previous_console_line));
    char signal[128];
    SDL_snprintf(signal, sizeof(signal), "signal.%s", action);
    if (editor_emit_signal_by_name(runtime, signal))
    {
        const char *current_action =
            slayer3d_properties_get_string(runtime->scene_state, "editor.tool.last_action", "");
        const char *current_console_line =
            slayer3d_properties_get_string(runtime->scene_state, "editor.console.history0", "");
        if (current_action[0] != '\0' && SDL_strcmp(current_action, previous_action) != 0 &&
            SDL_strcmp(current_console_line, previous_console_line) == 0)
        {
            editor_publish_console_message(runtime, current_action);
        }
        return true;
    }
    return false;
}

bool editor_handle_tool_mode_buttons(slayer3d_game_data_runtime *runtime, yyjson_val *editor, bool *out_consumed)
{
    if (out_consumed != NULL)
        *out_consumed = false;
    yyjson_val *buttons = obj_get(editor, "tool_mode_buttons");
    if (runtime == NULL || runtime->scene_state == NULL || !yyjson_is_arr(buttons))
        return true;
    editor_update_texture_edit_display(runtime);
    editor_update_global_edit_display(runtime);
    editor_update_settings_edit_display(runtime);
    slayer3d_input_manager *input = runtime_input(runtime);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (input == NULL || !slayer3d_input_get_mouse_position(input, &mouse_x, &mouse_y))
        return true;

    const bool clicked = slayer3d_input_is_mouse_button_pressed(input, SDL_BUTTON_LEFT);
    const bool left_down = slayer3d_input_is_mouse_button_down(input, SDL_BUTTON_LEFT);
    const bool released = slayer3d_input_is_mouse_button_released(input, SDL_BUTTON_LEFT);
    const float wheel_y = slayer3d_input_get_mouse_wheel_y(input);
    slayer3d_ui_layout_model *layout = NULL;
    const slayer3d_ui_layout_hit_region *hit = NULL;
    (void)editor_retained_ui_hit(runtime, mouse_x, mouse_y, &layout, &hit);
    editor_sync_ui_window_resolved_state(runtime, layout);
    if (editor_handle_ui_window_pointer(runtime, layout, hit, mouse_x, mouse_y, clicked, left_down, released))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    const bool window_event_active = editor_resolved_window_for_hit(layout, hit) != NULL;
    const bool property_focus_active = editor_property_edit_has_focus(runtime);
    const bool texture_focus_active = editor_texture_edit_has_focus(runtime);
    const bool global_focus_active = editor_global_edit_has_focus(runtime);
    const bool settings_focus_active = editor_settings_edit_has_focus(runtime);
    if (property_focus_active && clicked && !editor_hit_is_property_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.property.edit.focus", "");
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (texture_focus_active && clicked && !editor_hit_is_texture_edit_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.texture.edit.focus", "");
        editor_update_texture_edit_display(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (global_focus_active && clicked && !editor_hit_is_global_edit_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.global.data.edit.focus", "");
        editor_update_global_edit_display(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (settings_focus_active && clicked && !editor_hit_is_settings_edit_control(hit))
    {
        slayer3d_properties_set_string(runtime->scene_state, "editor.settings.edit.focus", "");
        editor_update_settings_edit_display(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (property_focus_active && !clicked)
    {
        (void)editor_update_property_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (texture_focus_active && !clicked)
    {
        (void)editor_update_texture_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (global_focus_active && !clicked)
    {
        (void)editor_update_global_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (settings_focus_active && !clicked)
    {
        (void)editor_update_settings_text_edit(runtime);
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false))
    {
        if (slayer3d_input_is_scancode_pressed(input, SDL_SCANCODE_ESCAPE))
        {
            editor_set_console_focus(runtime, false);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
            editor_refresh_console_lines(runtime);
            if (out_consumed != NULL)
                *out_consumed = true;
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (editor_console_copy_selection_if_requested(runtime, input))
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.console.selection.active", false))
    {
        if (released || !left_down)
        {
            (void)editor_console_set_selection_cursor(runtime, layout, mouse_x, mouse_y);
            slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
            editor_refresh_console_lines(runtime);
            (void)editor_console_copy_selection(runtime);
        }
        else
        {
            (void)editor_console_set_selection_cursor(runtime, layout, mouse_x, mouse_y);
        }
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    const char *scrollbar_drag_pane =
        slayer3d_properties_get_string(runtime->scene_state, "editor.ui.scrollbar.drag.pane", "");
    if (scrollbar_drag_pane[0] != '\0')
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.ui.scrollbar.drag.pane", "");
        }
        else
        {
            if (out_consumed != NULL)
                *out_consumed = true;
            (void)editor_update_scrollbar_drag(runtime, layout, scrollbar_drag_pane, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
    }
    if (wheel_y != 0.0f && editor_update_wheel_scroll(runtime, layout, mouse_x, mouse_y, wheel_y))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (clicked && !editor_hit_is_console(hit) &&
        slayer3d_properties_get_bool(runtime->scene_state, "editor.console.focused", false))
    {
        editor_set_console_focus(runtime, false);
        slayer3d_properties_set_bool(runtime->scene_state, "editor.console.selection.active", false);
        editor_refresh_console_lines(runtime);
    }
    const bool console_event_active =
        editor_hit_is_console(hit) && (clicked || released || left_down || wheel_y != 0.0f);
    if (editor_handle_bound_dropdown(runtime, hit, clicked))
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    if (editor_hit_is_toolbar(hit) || editor_hit_is_texture_viewer(hit) || editor_hit_is_file_menu(hit) ||
        editor_hit_is_global_panel(hit) || editor_hit_is_stair_panel(hit) || editor_hit_is_liquid_panel(hit) ||
        editor_hit_is_skybox_panel(hit) || editor_hit_is_actor_viewer(hit) || editor_hit_is_left_inspector(hit) ||
        editor_hit_is_outside_click_capture(hit) || console_event_active || window_event_active)
    {
        if (out_consumed != NULL)
            *out_consumed = true;
        if (editor_hit_is_console(hit))
        {
            editor_set_console_focus(runtime, true);
            if (clicked && !editor_hit_is_synthesized_scrollbar(hit))
            {
                if (editor_console_begin_selection(runtime, layout, mouse_x, mouse_y))
                {
                    slayer3d_ui_layout_destroy(layout);
                    return true;
                }
            }
        }
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            slayer3d_properties_set_string(runtime->scene_state, "editor.ui.scrollbar.drag.pane", "");
        }
        if (clicked && editor_hit_is_synthesized_scrollbar(hit))
        {
            slayer3d_properties_set_string(runtime->scene_state, "editor.ui.scrollbar.drag.pane", hit->owner_id);
            (void)editor_update_scrollbar_drag(runtime, layout, hit->owner_id, mouse_y);
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        if (clicked && editor_focus_settings_field(runtime, hit))
        {
            slayer3d_ui_layout_destroy(layout);
            return true;
        }
        char derived_action[96];
        const char *hit_action = editor_property_row_select_action(hit, derived_action, sizeof(derived_action));
        if (clicked && hit_action[0] != '\0' && !editor_texture_select_action_is_blocked(runtime, hit_action))
        {
            (void)editor_apply_widget_action(runtime, hit_action);
            if (editor_actor_select_action(hit_action))
            {
                slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", true);
                slayer3d_properties_set_string(runtime->scene_state, "editor.tool.last_action",
                                               "drag actor into viewport");
            }
        }
        slayer3d_ui_layout_destroy(layout);
        return true;
    }
    slayer3d_ui_layout_destroy(layout);

    if (slayer3d_properties_get_bool(runtime->scene_state, "editor.actor.drag.active", false))
    {
        if (released || !left_down)
        {
            slayer3d_properties_set_bool(runtime->scene_state, "editor.actor.drag.active", false);
            if (released && editor_actor_placement_mode(runtime) &&
                slayer3d_properties_get_bool(runtime->scene_state, "editor.placement_preview.active", false))
            {
                if (out_consumed != NULL)
                    *out_consumed = true;
                (void)editor_emit_signal_by_name(runtime, "signal.editor.command.commit");
                return true;
            }
        }
    }

    for (size_t i = 0, count = yyjson_arr_size(buttons); i < count; ++i)
    {
        yyjson_val *button = yyjson_arr_get(buttons, i);
        if (!yyjson_is_obj(button) || !editor_mouse_in_rect(mouse_x, mouse_y, obj_get(button, "button")))
            continue;
        if (out_consumed != NULL)
            *out_consumed = true;
        if (!clicked)
            return true;

        const char *mode = json_string(button, "mode", NULL);
        if (mode == NULL || mode[0] == '\0')
            return true;
        (void)slayer3d_game_data_set_editor_tool_mode(runtime, mode, json_string(button, "message", NULL));
        return true;
    }
    return true;
}

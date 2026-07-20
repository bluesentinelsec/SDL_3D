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

typedef struct editor_ui_window_config
{
    const char *id;
    const char *x_key;
    const char *y_key;
    const char *height_key;
    const char *resolved_height_key;
    const char *dock_key;
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
    float min_height;
    float max_height;
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
    out_config->height_key = json_string(window, "height_key", NULL);
    out_config->resolved_height_key = json_string(window, "resolved_height_key", NULL);
    out_config->dock_key = json_string(window, "dock_key", NULL);
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
    out_config->min_height = json_float(window, "min_height", 72.0f);
    out_config->max_height = json_float(window, "max_height", 360.0f);
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
}

static const char *editor_ui_window_dock_preview(const editor_ui_window_config *config, float mouse_x, float mouse_y,
                                                 float viewport_w, float viewport_h)
{
    if (config == NULL)
        return "none";

    const float left_distance = mouse_x;
    const float right_distance = viewport_w - mouse_x;
    const float bottom_distance = viewport_h - mouse_y;
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
                                            float viewport_w, float viewport_h)
{
    if (runtime == NULL || runtime->scene_state == NULL || config == NULL)
        return;

    slayer3d_properties *state = runtime->scene_state;
    const char *preview = editor_ui_window_dock_preview(config, mouse_x, mouse_y, viewport_w, viewport_h);
    slayer3d_ui_layout_rect preview_rect = {floating_x, floating_y, floating_w, floating_h};
    const slayer3d_ui_layout_dock dock = editor_ui_window_dock_from_string(preview);
    if (dock != SLAYER3D_UI_LAYOUT_DOCK_NONE)
    {
        (void)slayer3d_ui_layout_calculate_window_dock_rect(layout, config->id, dock, viewport_w, viewport_h,
                                                            &preview_rect);
    }

    slayer3d_properties_set_string(state, "editor.ui.window.pointer.preview", preview);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.x", preview_rect.x);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.y", preview_rect.y);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.w", preview_rect.w);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.preview.h", preview_rect.h);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.x", 0.0f);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.y", config->dock_top);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.w", viewport_w);
    slayer3d_properties_set_float(state, "editor.ui.window.pointer.canvas.h",
                                  SDL_max(viewport_h - config->dock_top - config->dock_bottom, 0.0f));
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
                const float origin_x =
                    slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_x", window->rect.x);
                const float origin_y =
                    slayer3d_properties_get_float(state, "editor.ui.window.pointer.origin_y", window->rect.y);
                float viewport_w = 0.0f;
                float viewport_h = 0.0f;
                slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
                const float floating_w = config.floating_width > 0.0f ? config.floating_width : window->rect.w;
                const float floating_h = config.floating_height > 0.0f ? config.floating_height : window->rect.h;
                const float x = origin_x + mouse_x - start_x;
                const float y = origin_y + mouse_y - start_y;
                slayer3d_properties_set_float(state, config.x_key, x);
                slayer3d_properties_set_float(state, config.y_key, y);
                editor_update_ui_window_preview(runtime, layout, &config, x, y, floating_w, floating_h, mouse_x,
                                                mouse_y, viewport_w, viewport_h);
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
                float viewport_w = 0.0f;
                float viewport_h = 0.0f;
                float x = slayer3d_properties_get_float(state, config.x_key, window->rect.x);
                float y = slayer3d_properties_get_float(state, config.y_key, window->rect.y);
                slayer3d_game_data_ui_viewport(runtime, &viewport_w, &viewport_h);
                editor_clamp_ui_window_to_titlebar(layout, &config, window, viewport_w, viewport_h, &x, &y);
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
    const bool starts_drag = config.drag_handle != NULL && SDL_strcmp(hit->id, config.drag_handle) == 0;
    const bool starts_resize = config.resize_handle != NULL && SDL_strcmp(hit->id, config.resize_handle) == 0 &&
                               window->dock != SLAYER3D_UI_LAYOUT_DOCK_LEFT &&
                               window->dock != SLAYER3D_UI_LAYOUT_DOCK_RIGHT;
    if (!starts_drag && !starts_resize)
        return false;

    slayer3d_properties_set_string(state, "editor.ui.window.pointer.id", window->id);
    slayer3d_properties_set_string(state, "editor.ui.window.pointer.mode", starts_drag ? "drag_pending" : "resize");
    if (starts_drag)
    {
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.start_x", mouse_x);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.start_y", mouse_y);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_x", window->rect.x);
        slayer3d_properties_set_float(state, "editor.ui.window.pointer.origin_y", window->rect.y);
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
    {"select", "select mode", NULL},
    {"brush", "brush tool", NULL},
    {"face", "face tool", NULL},
    {"edge", "edge tool", "select a brush before edge tool"},
    {"clip", "", NULL},
    {"vertex", "vertex tool", "select a brush before vertex tool"},
    {"rotate", "rotate tool", "select a brush before rotate tool"},
    {"scale", "scale tool", "select a brush before scale tool"},
    {"shear", "shear tool", "select a brush before shear tool"},
    {"liquid", "liquid tool", NULL},
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
    return true;
}

/* Toolbar actions forwarded verbatim as "signal.<action>". */
static const char *const editor_signal_action_names[] = {
    "editor.brush.duplicate",
    "editor.brush.flip_horizontal",
    "editor.brush.flip_vertical",
};

/* Action families whose members are all forwarded as "signal.<action>". */
static const char *const editor_signal_action_prefixes[] = {
    "editor.texture.",   "editor.palette.",  "editor.actor.",  "editor.things.",     "editor.file.",
    "editor.inspector.", "editor.property.", "editor.global.", "editor.visibility.", "editor.lock.",
    "editor.stair.",     "editor.sky.",      "editor.liquid.", "editor.console.",
};

static bool editor_action_routes_to_signal(const char *action)
{
    for (size_t i = 0; i < SDL_arraysize(editor_signal_action_names); ++i)
    {
        if (SDL_strcmp(action, editor_signal_action_names[i]) == 0)
            return true;
    }
    for (size_t i = 0; i < SDL_arraysize(editor_signal_action_prefixes); ++i)
    {
        if (SDL_strncmp(action, editor_signal_action_prefixes[i], SDL_strlen(editor_signal_action_prefixes[i])) == 0)
            return true;
    }
    return false;
}

static bool editor_apply_tool_action(slayer3d_game_data_runtime *runtime, const char *action)
{
    const char *mode = editor_mode_for_tool_action(action);
    if (mode != NULL)
        return slayer3d_game_data_set_editor_tool_mode(runtime, mode, NULL);
    if (runtime != NULL && editor_action_routes_to_signal(action))
    {
        char signal[128];
        SDL_snprintf(signal, sizeof(signal), "signal.%s", action);
        if (editor_emit_signal_by_name(runtime, signal))
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
    if (editor_hit_is_toolbar(hit) || editor_hit_is_texture_viewer(hit) || editor_hit_is_file_menu(hit) ||
        editor_hit_is_global_panel(hit) || editor_hit_is_stair_panel(hit) || editor_hit_is_liquid_panel(hit) ||
        editor_hit_is_skybox_panel(hit) || editor_hit_is_actor_viewer(hit) || editor_hit_is_left_inspector(hit) ||
        console_event_active || window_event_active)
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
        char derived_action[96];
        const char *hit_action = editor_property_row_select_action(hit, derived_action, sizeof(derived_action));
        if (clicked && hit_action[0] != '\0' && !editor_texture_select_action_is_blocked(runtime, hit_action))
        {
            (void)editor_apply_tool_action(runtime, hit_action);
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

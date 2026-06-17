/**
 * @file game_data_validation_ui.c
 * @brief UI validation for JSON-authored game data.
 */

#include "game_data_validation_internal.h"

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/ui_layout.h"

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return validation_obj_get(object, key);
}

static const char *json_string(yyjson_val *object, const char *key)
{
    return validation_json_string(object, key);
}

static slayer3d_color ui_widget_color_value(yyjson_val *value, slayer3d_color fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 3)
        return fallback;

    yyjson_val *r = yyjson_arr_get(value, 0);
    yyjson_val *g = yyjson_arr_get(value, 1);
    yyjson_val *b = yyjson_arr_get(value, 2);
    yyjson_val *a = yyjson_arr_get(value, 3);
    if (!yyjson_is_num(r) || !yyjson_is_num(g) || !yyjson_is_num(b))
        return fallback;

    return (slayer3d_color){
        (Uint8)SDL_clamp((int)yyjson_get_num(r), 0, 255),
        (Uint8)SDL_clamp((int)yyjson_get_num(g), 0, 255),
        (Uint8)SDL_clamp((int)yyjson_get_num(b), 0, 255),
        yyjson_is_num(a) ? (Uint8)SDL_clamp((int)yyjson_get_num(a), 0, 255) : fallback.a,
    };
}

static slayer3d_color ui_widget_color(yyjson_val *object, const char *key, slayer3d_color fallback)
{
    return ui_widget_color_value(obj_get(object, key), fallback);
}

static bool validate_ui_tool_binding(validation_context *ctx, yyjson_val *binding, const char *path,
                                     validation_names *names);
bool validate_ui_tool_color(validation_context *ctx, yyjson_val *object, const char *key, const char *path);

static bool is_non_empty_string(yyjson_val *object, const char *key)
{
    const char *value = json_string(object, key);
    return value != NULL && value[0] != '\0';
}

bool ui_metric_name_valid(const char *metric)
{
    if (metric == NULL || metric[0] == '\0')
        return false;

    static const char *const metrics[] = {
        "fps",
        "frame",
        "paused",
        "perf.frame_ms",
        "perf.update_cpu_ms",
        "perf.render_cpu_ms",
        "render.model_mesh_submissions_per_frame",
        "render.model_mesh_draws_per_frame",
        "render.model_triangles_per_frame",
        "render.geometry_draw_calls_per_frame",
        "render.static_mesh_instanced_draw_calls_per_frame",
        "render.static_mesh_instances_batched_per_frame",
        "render.static_mesh_draw_calls_saved_per_frame",
        "render.procedural_lod_candidates_per_frame",
        "render.procedural_lod_reduced_per_frame",
        "render.procedural_lod_authored_triangles_per_frame",
        "render.procedural_lod_resolved_triangles_per_frame",
        "render.procedural_lod_triangles_saved_per_frame",
        "render.model_lod_candidates_per_frame",
        "render.model_lod_culled_per_frame",
        "render.model_lod_triangles_saved_per_frame",
        "render.depth_prepass_draws_per_frame",
        "render.depth_prepass_triangles_per_frame",
        "render.depth_prepass_samples_per_frame",
        "render.geometry_samples_per_frame",
        "render.light_candidates_per_frame",
        "render.lights_selected_per_frame",
        "render.light_selection_draws_per_frame",
        "render.light_selection_ratio",
        "render.world_scale",
        "render.world_width",
        "render.world_height",
        "render.window_pixel_width",
        "render.window_pixel_height",
        "render.window_pixel_density",
        "brush.trace_count",
        "brush.world_instance_count",
        "brush.world_bounds_reject_count",
        "brush.brush_count",
        "brush.contents_reject_count",
        "brush.bounds_reject_count",
        "brush.collision_candidate_count",
        "brush.hit_count",
        "brush.render_mesh_submissions",
        "brush.render_mesh_culled",
        "brush.render_mesh_draws",
        "brush.render_triangles_submitted",
        "brush.compile_face_count",
        "brush.compile_rendered_face_count",
        "brush.compile_culled_face_count",
        "brush.compile_triangle_count",
        "brush.compile_chunk_count",
        "brush.collision_chunk_count",
        "brush.collision_chunk_reject_count",
        "brush.render_chunk_draws",
        "brush.render_chunk_brushes_drawn",
        "brush.frustum_brush_candidates",
        "brush.frustum_brush_culled",
        "brush.frustum_triangles_culled",
        "brush.visibility_brush_candidates",
        "brush.visibility_brush_visible",
        "brush.visibility_brush_occluded",
        "brush.visibility_triangles_culled",
        "brush.visibility_grid_cache_hits",
        "brush.visibility_grid_cache_misses",
    };

    for (size_t i = 0; i < SDL_arraysize(metrics); ++i)
    {
        if (SDL_strcmp(metric, metrics[i]) == 0)
            return true;
    }
    return false;
}

static bool is_json_scalar(yyjson_val *value)
{
    return yyjson_is_str(value) || yyjson_is_int(value) || yyjson_is_real(value) || yyjson_is_bool(value);
}

static bool ui_widget_type_valid(const char *type)
{
    static const char *const types[] = {
        "panel",    "toolbar",   "row",    "column",  "button",   "label",
        "dropdown", "tab_strip", "spacer", "console", "log_view",
    };
    if (type == NULL || type[0] == '\0')
        return false;
    for (size_t i = 0; i < SDL_arraysize(types); ++i)
    {
        if (SDL_strcmp(type, types[i]) == 0)
            return true;
    }
    return false;
}

static bool ui_widget_layout_valid(const char *layout)
{
    return layout == NULL || layout[0] == '\0' || SDL_strcmp(layout, "none") == 0 || SDL_strcmp(layout, "row") == 0 ||
           SDL_strcmp(layout, "column") == 0;
}

static bool ui_widget_size_value_valid(yyjson_val *value)
{
    if (value == NULL)
        return true;
    if (yyjson_is_str(value))
    {
        const char *mode = yyjson_get_str(value);
        return mode != NULL && SDL_strcmp(mode, "fill") == 0;
    }
    return yyjson_is_num(value) && yyjson_get_num(value) > 0.0;
}

static bool ui_widget_optional_number_non_negative(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return value == NULL || (yyjson_is_num(value) && yyjson_get_num(value) >= 0.0);
}

static bool ui_widget_optional_number(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return value == NULL || yyjson_is_num(value);
}

static bool ui_widget_optional_integer(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return value == NULL || yyjson_is_int(value);
}

static bool ui_widget_optional_bool(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return value == NULL || yyjson_is_bool(value);
}

static bool ui_widget_optional_positive_number(yyjson_val *object, const char *key)
{
    yyjson_val *value = obj_get(object, key);
    return value == NULL || (yyjson_is_num(value) && yyjson_get_num(value) > 0.0);
}

static bool ui_widget_text_align_valid(const char *align)
{
    return align == NULL || SDL_strcmp(align, "left") == 0 || SDL_strcmp(align, "center") == 0 ||
           SDL_strcmp(align, "right") == 0;
}

static bool ui_widget_optional_string_len(yyjson_val *object, const char *key, size_t max_size)
{
    yyjson_val *value = obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value))
        return false;
    const char *text = yyjson_get_str(value);
    return text != NULL && SDL_strlen(text) < max_size;
}

static bool ui_widget_string_format_valid(const char *format)
{
    if (format == NULL)
        return true;
    for (const char *cursor = format; *cursor != '\0'; ++cursor)
    {
        if (*cursor != '%')
            continue;
        ++cursor;
        if (*cursor != '%' && *cursor != 's')
            return false;
        if (*cursor == '\0')
            return false;
    }
    return true;
}

static bool ui_widget_string_placeholder_count(const char *format, size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (format == NULL)
        return true;
    size_t count = 0;
    for (const char *cursor = format; *cursor != '\0'; ++cursor)
    {
        if (*cursor != '%')
            continue;
        ++cursor;
        if (*cursor == '\0')
            return false;
        if (*cursor == 's')
            ++count;
    }
    if (out_count != NULL)
        *out_count = count;
    return true;
}

static bool ui_widget_optional_non_empty_string_len(yyjson_val *object, const char *key, size_t max_size)
{
    yyjson_val *value = obj_get(object, key);
    if (value == NULL)
        return true;
    if (!yyjson_is_str(value))
        return false;
    const char *text = yyjson_get_str(value);
    return text != NULL && text[0] != '\0' && SDL_strlen(text) < max_size;
}

typedef struct ui_widget_name_set
{
    const char **ids;
    size_t count;
    size_t capacity;
} ui_widget_name_set;

static void ui_widget_name_set_destroy(ui_widget_name_set *set)
{
    if (set == NULL)
        return;
    SDL_free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

static bool ui_widget_name_set_contains(const ui_widget_name_set *set, const char *id)
{
    if (set == NULL || id == NULL)
        return false;
    for (size_t i = 0; i < set->count; ++i)
    {
        if (SDL_strcmp(set->ids[i], id) == 0)
            return true;
    }
    return false;
}

static bool ui_widget_name_set_add(ui_widget_name_set *set, const char *id)
{
    if (set == NULL || id == NULL || id[0] == '\0' || ui_widget_name_set_contains(set, id))
        return false;
    if (set->count >= set->capacity)
    {
        const size_t new_capacity = set->capacity > 0 ? set->capacity * 2U : 32U;
        const char **ids = (const char **)SDL_realloc(set->ids, new_capacity * sizeof(*ids));
        if (ids == NULL)
            return false;
        set->ids = ids;
        set->capacity = new_capacity;
    }
    set->ids[set->count++] = id;
    return true;
}

static bool validate_ui_widget_node(validation_context *ctx, yyjson_val *node, const char *path,
                                    ui_widget_name_set *ids, validation_names *names)
{
    if (!yyjson_is_obj(node))
        return validation_error(ctx, path, "UI widget nodes must be objects");
    const char *id = json_string(node, "id");
    if (id == NULL || id[0] == '\0')
        return validation_error(ctx, path, "UI widget node requires a non-empty id");
    if (!ui_widget_name_set_add(ids, id))
        return validation_error(ctx, path, "UI widget id '%s' is duplicated", id);
    const char *type = json_string(node, "type");
    if (!ui_widget_type_valid(type))
        return validation_error(ctx, path, "unsupported UI widget type '%s'", type != NULL ? type : "<missing>");
    const char *layout = json_string(node, "layout");
    if (!ui_widget_layout_valid(layout))
        return validation_error(ctx, path, "unsupported UI widget layout '%s'", layout);
    if (!ui_widget_size_value_valid(obj_get(node, "w")) || !ui_widget_size_value_valid(obj_get(node, "width")))
        return validation_error(ctx, path, "UI widget width must be positive or 'fill'");
    if (!ui_widget_size_value_valid(obj_get(node, "h")) || !ui_widget_size_value_valid(obj_get(node, "height")))
        return validation_error(ctx, path, "UI widget height must be positive or 'fill'");
    if (!ui_widget_optional_number(node, "x") || !ui_widget_optional_number(node, "y"))
        return validation_error(ctx, path, "UI widget x/y must be numeric when authored");
    if (!ui_widget_optional_number_non_negative(node, "padding"))
        return validation_error(ctx, path, "UI widget padding must be non-negative");
    if (!ui_widget_optional_number_non_negative(node, "gap"))
        return validation_error(ctx, path, "UI widget gap must be non-negative");
    if (!ui_widget_optional_number_non_negative(node, "border_thickness"))
        return validation_error(ctx, path, "UI widget border_thickness must be non-negative");
    if (!validate_ui_tool_color(ctx, node, "color", path) || !validate_ui_tool_color(ctx, node, "fill_color", path) ||
        !validate_ui_tool_color(ctx, node, "border_color", path) ||
        !validate_ui_tool_color(ctx, node, "text_color", path))
    {
        return false;
    }
    if (!ui_widget_optional_positive_number(node, "text_scale"))
        return validation_error(ctx, path, "UI widget text_scale must be positive when authored");
    if (!ui_widget_text_align_valid(json_string(node, "align")))
        return validation_error(ctx, path, "UI widget align must be left, center, or right when authored");
    if (!ui_widget_optional_integer(node, "layer") || !ui_widget_optional_integer(node, "z"))
        return validation_error(ctx, path, "UI widget layer/z must be an integer when authored");
    if (!ui_widget_optional_bool(node, "interactive"))
        return validation_error(ctx, path, "UI widget interactive must be a boolean when authored");
    if (!ui_widget_optional_string_len(node, "text", SLAYER3D_UI_LAYOUT_TEXT_MAX) ||
        !ui_widget_optional_string_len(node, "label", SLAYER3D_UI_LAYOUT_TEXT_MAX))
    {
        return validation_error(ctx, path, "UI widget text/label must be a string shorter than %d bytes",
                                SLAYER3D_UI_LAYOUT_TEXT_MAX);
    }
    if (!ui_widget_optional_string_len(node, "format", SLAYER3D_UI_LAYOUT_TEXT_MAX) ||
        !ui_widget_optional_string_len(node, "text_format", SLAYER3D_UI_LAYOUT_TEXT_MAX))
    {
        return validation_error(ctx, path, "UI widget format/text_format must be a string shorter than %d bytes",
                                SLAYER3D_UI_LAYOUT_TEXT_MAX);
    }
    if (!ui_widget_string_format_valid(json_string(node, "format")))
        return validation_error(ctx, path, "UI widget format may only use %%s string placeholders");
    const char *font = json_string(node, "font");
    if (obj_get(node, "font") != NULL &&
        !ui_widget_optional_non_empty_string_len(node, "font", SLAYER3D_UI_LAYOUT_FONT_MAX))
    {
        return validation_error(ctx, path, "UI widget font must be a non-empty string shorter than %d bytes",
                                SLAYER3D_UI_LAYOUT_FONT_MAX);
    }
    if (font != NULL && !require_ref(ctx, &names->fonts, "font asset", font, path))
    {
        return false;
    }
    if (!ui_widget_optional_non_empty_string_len(node, "text_value_key", SLAYER3D_UI_LAYOUT_ACTION_MAX) ||
        !ui_widget_optional_non_empty_string_len(node, "open_key", SLAYER3D_UI_LAYOUT_ACTION_MAX) ||
        !ui_widget_optional_non_empty_string_len(node, "selected_value_key", SLAYER3D_UI_LAYOUT_ACTION_MAX))
    {
        return validation_error(ctx, path, "UI widget state keys must be non-empty strings shorter than %d bytes",
                                SLAYER3D_UI_LAYOUT_ACTION_MAX);
    }
    if (!ui_widget_optional_non_empty_string_len(node, "action", SLAYER3D_UI_LAYOUT_ACTION_MAX))
        return validation_error(ctx, path, "UI widget action must be a non-empty string shorter than %d bytes",
                                SLAYER3D_UI_LAYOUT_ACTION_MAX);
    if (!ui_widget_optional_bool(node, "selected"))
        return validation_error(ctx, path, "UI widget selected must be a boolean when authored");

    char condition_path[PATH_BUFFER_SIZE];
    format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
    if (!validate_data_condition(ctx, obj_get(node, "visible_if"), condition_path, names))
        return false;
    format_path(condition_path, sizeof(condition_path), "%s.selected_if", path);
    if (!validate_data_condition(ctx, obj_get(node, "selected_if"), condition_path, names))
        return false;

    yyjson_val *bindings = obj_get(node, "bindings");
    if (bindings != NULL && !yyjson_is_arr(bindings))
        return validation_error(ctx, path, "UI widget bindings must be an array");
    size_t placeholder_count = 0;
    const char *format = json_string(node, "format");
    if (!ui_widget_string_placeholder_count(format, &placeholder_count))
        return validation_error(ctx, path, "UI widget format may only use %%s string placeholders");
    if (format != NULL && (!yyjson_is_arr(bindings) || yyjson_arr_size(bindings) != placeholder_count))
    {
        return validation_error(ctx, path,
                                "UI widget format placeholder count must match the number of authored bindings");
    }
    for (size_t binding_index = 0; yyjson_is_arr(bindings) && binding_index < yyjson_arr_size(bindings);
         ++binding_index)
    {
        char binding_path[PATH_BUFFER_SIZE];
        format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, binding_index);
        if (!validate_ui_tool_binding(ctx, yyjson_arr_get(bindings, binding_index), binding_path, names))
            return false;
    }

    yyjson_val *options = obj_get(node, "options");
    if (options != NULL)
    {
        if (SDL_strcmp(type, "dropdown") != 0)
            return validation_error(ctx, path, "UI widget options are only supported by dropdown widgets");
        if (!yyjson_is_arr(options))
            return validation_error(ctx, path, "UI dropdown options must be an array");
        if (yyjson_arr_size(options) > SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX)
        {
            return validation_error(ctx, path, "UI dropdown options must contain at most %d entries",
                                    SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX);
        }
        for (size_t option_index = 0; option_index < yyjson_arr_size(options); ++option_index)
        {
            yyjson_val *option = yyjson_arr_get(options, option_index);
            if (!yyjson_is_str(option))
                return validation_error(ctx, path, "UI dropdown options must be strings");
            const char *text = yyjson_get_str(option);
            if (text == NULL || text[0] == '\0' || SDL_strlen(text) >= SLAYER3D_UI_LAYOUT_TEXT_MAX)
            {
                return validation_error(ctx, path,
                                        "UI dropdown options must be non-empty strings shorter than %d bytes",
                                        SLAYER3D_UI_LAYOUT_TEXT_MAX);
            }
        }
        yyjson_val *selected_index = obj_get(node, "selected_index");
        if (selected_index != NULL)
        {
            if (!yyjson_is_int(selected_index))
                return validation_error(ctx, path, "UI dropdown selected_index must be an integer");
            const int index = (int)yyjson_get_int(selected_index);
            if (index < -1 || (yyjson_arr_size(options) == 0 && index > 0) ||
                (yyjson_arr_size(options) > 0 && (size_t)index >= yyjson_arr_size(options)))
            {
                return validation_error(ctx, path, "UI dropdown selected_index is out of range");
            }
        }
    }
    else if (obj_get(node, "selected_index") != NULL)
    {
        return validation_error(ctx, path, "UI dropdown selected_index requires options");
    }
    if (SDL_strcmp(type, "dropdown") != 0 && obj_get(node, "open") != NULL)
        return validation_error(ctx, path, "UI widget open is only supported by dropdown widgets");
    if (!ui_widget_optional_bool(node, "open"))
        return validation_error(ctx, path, "UI dropdown open must be a boolean when authored");
    if (SDL_strcmp(type, "dropdown") != 0 && obj_get(node, "option_height") != NULL)
        return validation_error(ctx, path, "UI widget option_height is only supported by dropdown widgets");
    if (!ui_widget_optional_positive_number(node, "option_height"))
        return validation_error(ctx, path, "UI dropdown option_height must be positive when authored");

    yyjson_val *children = obj_get(node, "children");
    if (children != NULL && !yyjson_is_arr(children))
        return validation_error(ctx, path, "UI widget children must be an array");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
    {
        char child_path[PATH_BUFFER_SIZE];
        format_path(child_path, sizeof(child_path), "%s.children[%zu]", path, i);
        if (!validate_ui_widget_node(ctx, yyjson_arr_get(children, i), child_path, ids, names))
            return false;
    }
    return true;
}

static slayer3d_ui_layout_node_type parse_ui_widget_type(const char *type)
{
    if (type == NULL)
        return SLAYER3D_UI_LAYOUT_NODE_PANEL;
    if (SDL_strcmp(type, "toolbar") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_TOOLBAR;
    if (SDL_strcmp(type, "row") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_ROW;
    if (SDL_strcmp(type, "column") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_COLUMN;
    if (SDL_strcmp(type, "button") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_BUTTON;
    if (SDL_strcmp(type, "label") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_LABEL;
    if (SDL_strcmp(type, "dropdown") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_DROPDOWN;
    if (SDL_strcmp(type, "tab_strip") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_TAB_STRIP;
    if (SDL_strcmp(type, "spacer") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_SPACER;
    if (SDL_strcmp(type, "console") == 0 || SDL_strcmp(type, "log_view") == 0)
        return SLAYER3D_UI_LAYOUT_NODE_CONSOLE;
    return SLAYER3D_UI_LAYOUT_NODE_PANEL;
}

static slayer3d_ui_layout_axis parse_ui_widget_axis(const char *layout)
{
    if (layout != NULL && SDL_strcmp(layout, "row") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_ROW;
    if (layout != NULL && SDL_strcmp(layout, "column") == 0)
        return SLAYER3D_UI_LAYOUT_AXIS_COLUMN;
    return SLAYER3D_UI_LAYOUT_AXIS_NONE;
}

static yyjson_val *ui_widget_size_value(yyjson_val *node, const char *short_key, const char *long_key)
{
    yyjson_val *value = obj_get(node, short_key);
    return value != NULL ? value : obj_get(node, long_key);
}

static slayer3d_ui_layout_size_mode parse_ui_widget_size_mode(yyjson_val *value)
{
    return yyjson_is_str(value) && SDL_strcmp(yyjson_get_str(value), "fill") == 0 ? SLAYER3D_UI_LAYOUT_SIZE_FILL
                                                                                  : SLAYER3D_UI_LAYOUT_SIZE_FIXED;
}

static float parse_ui_widget_size_value(yyjson_val *value, float fallback)
{
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

static float parse_ui_widget_float(yyjson_val *node, const char *key, float fallback)
{
    yyjson_val *value = obj_get(node, key);
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

static int parse_ui_widget_int(yyjson_val *node, const char *primary_key, const char *secondary_key, int fallback)
{
    yyjson_val *value = obj_get(node, primary_key);
    if (value == NULL)
        value = obj_get(node, secondary_key);
    return yyjson_is_int(value) ? (int)yyjson_get_int(value) : fallback;
}

static bool parse_ui_widget_bool(yyjson_val *node, const char *key, bool fallback)
{
    yyjson_val *value = obj_get(node, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static slayer3d_ui_layout_text_align parse_ui_widget_text_align(const char *align)
{
    if (align != NULL && SDL_strcmp(align, "left") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_LEFT;
    if (align != NULL && SDL_strcmp(align, "center") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_CENTER;
    if (align != NULL && SDL_strcmp(align, "right") == 0)
        return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_RIGHT;
    return SLAYER3D_UI_LAYOUT_TEXT_ALIGN_AUTO;
}

static const char *parse_ui_widget_string(yyjson_val *node, const char *primary_key, const char *secondary_key)
{
    const char *value = json_string(node, primary_key);
    return value != NULL ? value : json_string(node, secondary_key);
}

static bool parse_ui_widget_node(validation_context *ctx, yyjson_val *node, const char *path, const char *parent_id,
                                 slayer3d_ui_layout_model *layout)
{
    yyjson_val *width = ui_widget_size_value(node, "w", "width");
    yyjson_val *height = ui_widget_size_value(node, "h", "height");
    slayer3d_ui_layout_node_desc desc;
    SDL_zero(desc);
    desc.id = json_string(node, "id");
    desc.parent_id = parent_id;
    desc.type = parse_ui_widget_type(json_string(node, "type"));
    desc.axis = parse_ui_widget_axis(json_string(node, "layout"));
    desc.width_mode = width != NULL ? parse_ui_widget_size_mode(width) : SLAYER3D_UI_LAYOUT_SIZE_FILL;
    desc.height_mode = height != NULL ? parse_ui_widget_size_mode(height) : SLAYER3D_UI_LAYOUT_SIZE_FILL;
    desc.rect.x = parse_ui_widget_float(node, "x", 0.0f);
    desc.rect.y = parse_ui_widget_float(node, "y", 0.0f);
    desc.rect.w = parse_ui_widget_size_value(width, 1.0f);
    desc.rect.h = parse_ui_widget_size_value(height, 1.0f);
    desc.padding = parse_ui_widget_float(node, "padding", 0.0f);
    desc.gap = parse_ui_widget_float(node, "gap", 0.0f);
    desc.clip_children = parse_ui_widget_bool(node, "clip_children", false);
    desc.clip_rect_id = json_string(node, "clip_rect_id");
    desc.layer = parse_ui_widget_int(node, "layer", "z", 0);
    desc.interactive = parse_ui_widget_bool(node, "interactive", false);
    desc.text = parse_ui_widget_string(node, "text", "label");
    desc.font = json_string(node, "font");
    desc.action = json_string(node, "action");
    desc.has_text_color = obj_get(node, "text_color") != NULL;
    desc.text_color = ui_widget_color(node, "text_color", (slayer3d_color){0, 0, 0, 0});
    desc.text_scale = parse_ui_widget_float(node, "text_scale", 0.0f);
    desc.text_align = parse_ui_widget_text_align(json_string(node, "align"));
    yyjson_val *fill_color = obj_get(node, "color");
    if (fill_color == NULL)
        fill_color = obj_get(node, "fill_color");
    desc.has_fill_color = fill_color != NULL;
    desc.fill_color = ui_widget_color_value(fill_color, (slayer3d_color){0, 0, 0, 0});
    desc.has_border_color = obj_get(node, "border_color") != NULL;
    desc.border_color = ui_widget_color(node, "border_color", (slayer3d_color){0, 0, 0, 0});
    desc.border_thickness = parse_ui_widget_float(node, "border_thickness", 0.0f);
    desc.selected = parse_ui_widget_bool(node, "selected", false);
    desc.selected_index = parse_ui_widget_int(node, "selected_index", "selected_index", 0);
    desc.open = parse_ui_widget_bool(node, "open", false);
    desc.option_height = parse_ui_widget_float(node, "option_height", 0.0f);
    const char *options[SLAYER3D_UI_LAYOUT_DROPDOWN_OPTION_MAX];
    yyjson_val *option_values = obj_get(node, "options");
    if (yyjson_is_arr(option_values))
    {
        desc.option_count = (int)yyjson_arr_size(option_values);
        desc.options = options;
        for (int i = 0; i < desc.option_count; ++i)
            options[i] = yyjson_get_str(yyjson_arr_get(option_values, (size_t)i));
    }
    if (!slayer3d_ui_layout_add_node(layout, &desc))
        return validation_error(ctx, path, "UI widget node could not be added to retained layout");

    yyjson_val *children = obj_get(node, "children");
    for (size_t i = 0; yyjson_is_arr(children) && i < yyjson_arr_size(children); ++i)
    {
        char child_path[PATH_BUFFER_SIZE];
        format_path(child_path, sizeof(child_path), "%s.children[%zu]", path, i);
        if (!parse_ui_widget_node(ctx, yyjson_arr_get(children, i), child_path, desc.id, layout))
            return false;
    }
    return true;
}

static bool validate_ui_widgets(validation_context *ctx, yyjson_val *widgets, const char *path, validation_names *names)
{
    if (widgets == NULL)
        return true;
    if (!yyjson_is_arr(widgets))
        return validation_error(ctx, path, "UI widgets must be an array");
    ui_widget_name_set ids = {0};
    bool ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(widgets); ++i)
    {
        char widget_path[PATH_BUFFER_SIZE];
        format_path(widget_path, sizeof(widget_path), "%s[%zu]", path, i);
        ok = validate_ui_widget_node(ctx, yyjson_arr_get(widgets, i), widget_path, &ids, names);
    }
    ui_widget_name_set_destroy(&ids);
    if (!ok)
        return false;

    slayer3d_ui_layout_model *layout = NULL;
    if (!slayer3d_ui_layout_create(&layout))
        return validation_error(ctx, path, "failed to allocate UI widget layout");
    ok = true;
    for (size_t i = 0; ok && i < yyjson_arr_size(widgets); ++i)
    {
        char widget_path[PATH_BUFFER_SIZE];
        format_path(widget_path, sizeof(widget_path), "%s[%zu]", path, i);
        ok = parse_ui_widget_node(ctx, yyjson_arr_get(widgets, i), widget_path, NULL, layout);
    }
    if (ok && !slayer3d_ui_layout_resolve(layout, 1280.0f, 720.0f))
        ok = validation_error(ctx, path, "UI widget layout could not be resolved");
    slayer3d_ui_layout_destroy(layout);
    return ok;
}

bool validate_ui_tool_color(validation_context *ctx, yyjson_val *object, const char *key, const char *path)
{
    yyjson_val *value = obj_get(object, key);
    if (value != NULL && !is_exact_vec3_or_vec4_array(value))
        return validation_error(ctx, path, "UI tooling %s must be a vec3 or vec4 color", key);
    return true;
}

static bool validate_ui_tool_binding(validation_context *ctx, yyjson_val *binding, const char *path,
                                     validation_names *names)
{
    if (!yyjson_is_obj(binding))
        return validation_error(ctx, path, "UI tooling binding must be an object");
    const char *type = json_string(binding, "type");
    if (SDL_strcmp(type != NULL ? type : "", "scene_state") == 0)
    {
        if (!is_non_empty_string(binding, "key"))
            return validation_error(ctx, path, "UI tooling scene_state binding requires a non-empty key");
    }
    else if (SDL_strcmp(type != NULL ? type : "", "property") == 0)
    {
        if (!require_ref(ctx, &names->entities, "entity", json_string(binding, "entity"), path))
            return false;
        if (!is_non_empty_string(binding, "key"))
            return validation_error(ctx, path, "UI tooling property binding requires a non-empty key");
    }
    else if (SDL_strcmp(type != NULL ? type : "", "metric") == 0)
    {
        const char *metric = json_string(binding, "metric");
        if (!ui_metric_name_valid(metric))
            return validation_error(ctx, path, "unsupported UI tooling metric '%s'",
                                    metric != NULL ? metric : "<missing>");
    }
    else
    {
        return validation_error(ctx, path, "unsupported UI tooling binding type '%s'",
                                type != NULL ? type : "<missing>");
    }

    yyjson_val *fallback = obj_get(binding, "default");
    if (fallback != NULL && !is_json_scalar(fallback))
        return validation_error(ctx, path, "UI tooling binding default must be scalar");
    return true;
}

bool validate_ui_panels(validation_context *ctx, yyjson_val *panels, const char *path, validation_names *names)
{
    if (panels == NULL)
        return true;
    if (!yyjson_is_arr(panels))
        return validation_error(ctx, path, "UI panels must be an array");
    for (size_t i = 0; i < yyjson_arr_size(panels); ++i)
    {
        char panel_path[PATH_BUFFER_SIZE];
        format_path(panel_path, sizeof(panel_path), "%s[%zu]", path, i);
        yyjson_val *panel = yyjson_arr_get(panels, i);
        if (!yyjson_is_obj(panel))
            return validation_error(ctx, panel_path, "UI panel entries must be objects");
        if (!is_non_empty_string(panel, "name"))
            return validation_error(ctx, panel_path, "UI panel requires a non-empty name");
        yyjson_val *width = obj_get(panel, "w");
        if (width == NULL)
            width = obj_get(panel, "width");
        yyjson_val *height = obj_get(panel, "h");
        if (height == NULL)
            height = obj_get(panel, "height");
        if (!yyjson_is_num(width) || yyjson_get_num(width) <= 0.0)
            return validation_error(ctx, panel_path, "UI panel width must be positive");
        if (!yyjson_is_num(height) || yyjson_get_num(height) <= 0.0)
            return validation_error(ctx, panel_path, "UI panel height must be positive");
        yyjson_val *border = obj_get(panel, "border_thickness");
        if (border != NULL && (!yyjson_is_num(border) || yyjson_get_num(border) < 0.0))
            return validation_error(ctx, panel_path, "UI panel border_thickness must be non-negative");
        if (!validate_ui_tool_color(ctx, panel, "color", panel_path) ||
            !validate_ui_tool_color(ctx, panel, "border_color", panel_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", panel_path);
        if (!validate_data_condition(ctx, obj_get(panel, "visible_if"), condition_path, names))
            return false;
    }
    return true;
}

bool validate_ui_inspectors(validation_context *ctx, yyjson_val *inspectors, const char *path, validation_names *names)
{
    if (inspectors == NULL)
        return true;
    if (!yyjson_is_arr(inspectors))
        return validation_error(ctx, path, "UI inspectors must be an array");
    for (size_t i = 0; i < yyjson_arr_size(inspectors); ++i)
    {
        char inspector_path[PATH_BUFFER_SIZE];
        format_path(inspector_path, sizeof(inspector_path), "%s[%zu]", path, i);
        yyjson_val *inspector = yyjson_arr_get(inspectors, i);
        if (!yyjson_is_obj(inspector))
            return validation_error(ctx, inspector_path, "UI inspector entries must be objects");
        if (!is_non_empty_string(inspector, "name"))
            return validation_error(ctx, inspector_path, "UI inspector requires a non-empty name");
        if (json_string(inspector, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(inspector, "font"), inspector_path))
            return false;
        yyjson_val *width = obj_get(inspector, "w");
        if (width == NULL)
            width = obj_get(inspector, "width");
        if (width != NULL && (!yyjson_is_num(width) || yyjson_get_num(width) <= 0.0))
            return validation_error(ctx, inspector_path, "UI inspector width must be positive");
        yyjson_val *row_height = obj_get(inspector, "row_height");
        if (row_height != NULL && (!yyjson_is_num(row_height) || yyjson_get_num(row_height) <= 0.0))
            return validation_error(ctx, inspector_path, "UI inspector row_height must be positive");
        yyjson_val *rows = obj_get(inspector, "rows");
        if (!yyjson_is_arr(rows))
            return validation_error(ctx, inspector_path, "UI inspector requires a rows array");
        if (!validate_ui_tool_color(ctx, inspector, "background_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "row_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "title_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "label_color", inspector_path) ||
            !validate_ui_tool_color(ctx, inspector, "value_color", inspector_path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", inspector_path);
        if (!validate_data_condition(ctx, obj_get(inspector, "visible_if"), condition_path, names))
            return false;
        for (size_t row_index = 0; row_index < yyjson_arr_size(rows); ++row_index)
        {
            char row_path[PATH_BUFFER_SIZE];
            format_path(row_path, sizeof(row_path), "%s.rows[%zu]", inspector_path, row_index);
            yyjson_val *row = yyjson_arr_get(rows, row_index);
            if (!yyjson_is_obj(row))
                return validation_error(ctx, row_path, "UI inspector rows must be objects");
            if (!is_non_empty_string(row, "label"))
                return validation_error(ctx, row_path, "UI inspector row requires a non-empty label");
            yyjson_val *binding = obj_get(row, "binding");
            yyjson_val *value = obj_get(row, "value");
            if ((binding == NULL) == (value == NULL))
                return validation_error(ctx, row_path, "UI inspector row requires exactly one of binding or value");
            if (binding != NULL)
            {
                char binding_path[PATH_BUFFER_SIZE];
                format_path(binding_path, sizeof(binding_path), "%s.binding", row_path);
                if (!validate_ui_tool_binding(ctx, binding, binding_path, names))
                    return false;
            }
            else if (!is_json_scalar(value))
            {
                return validation_error(ctx, row_path, "UI inspector row value must be scalar");
            }
            yyjson_val *empty = obj_get(row, "empty");
            if (empty != NULL && !yyjson_is_str(empty))
                return validation_error(ctx, row_path, "UI inspector row empty value must be a string");
        }
    }
    return true;
}

bool validate_ui(validation_context *ctx, yyjson_val *root, validation_names *names)
{
    yyjson_val *ui = obj_get(root, "ui");
    yyjson_val *texts = obj_get(ui, "text");
    yyjson_val *images = obj_get(ui, "images");
    yyjson_val *rects = obj_get(ui, "rects");
    yyjson_val *menus = obj_get(ui, "menus");
    yyjson_val *panels = obj_get(ui, "panels");
    yyjson_val *inspectors = obj_get(ui, "inspectors");
    yyjson_val *widgets = obj_get(ui, "widgets");
    if (texts == NULL && images == NULL && rects == NULL && menus == NULL && panels == NULL && inspectors == NULL &&
        widgets == NULL)
    {
        return true;
    }
    if (texts != NULL && !yyjson_is_arr(texts))
        return validation_error(ctx, "$.ui.text", "UI text must be an array");
    if (images != NULL && !yyjson_is_arr(images))
        return validation_error(ctx, "$.ui.images", "UI images must be an array");
    if (rects != NULL && !yyjson_is_arr(rects))
        return validation_error(ctx, "$.ui.rects", "UI rectangles must be an array");
    if (menus != NULL && !yyjson_is_arr(menus))
        return validation_error(ctx, "$.ui.menus", "UI menus must be an array");
    if (!validate_ui_panels(ctx, panels, "$.ui.panels", names) ||
        !validate_ui_inspectors(ctx, inspectors, "$.ui.inspectors", names) ||
        !validate_ui_widgets(ctx, widgets, "$.ui.widgets", names))
    {
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(texts); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.text[%zu]", i);
        yyjson_val *text = yyjson_arr_get(texts, i);
        if (!yyjson_is_obj(text))
            return validation_error(ctx, path, "UI text entries must be objects");
        if (json_string(text, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(text, "font"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(text, "visible_if"), condition_path, names))
            return false;

        yyjson_val *bindings = obj_get(text, "bindings");
        for (size_t b = 0; yyjson_is_arr(bindings) && b < yyjson_arr_size(bindings); ++b)
        {
            char binding_path[PATH_BUFFER_SIZE];
            format_path(binding_path, sizeof(binding_path), "%s.bindings[%zu]", path, b);
            yyjson_val *binding = yyjson_arr_get(bindings, b);
            const char *type = json_string(binding, "type");
            if (SDL_strcmp(type != NULL ? type : "", "property") == 0)
            {
                if (!require_ref(ctx, &names->entities, "entity", json_string(binding, "entity"), binding_path))
                    return false;
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "UI property binding requires a non-empty key");
            }
            else if (SDL_strcmp(type != NULL ? type : "", "metric") == 0)
            {
                const char *metric = json_string(binding, "metric");
                if (!ui_metric_name_valid(metric))
                    return validation_error(ctx, binding_path, "unsupported UI metric '%s'",
                                            metric != NULL ? metric : "<missing>");
            }
            else
            {
                if (SDL_strcmp(type != NULL ? type : "", "scene_state") != 0)
                    return validation_error(ctx, binding_path, "unsupported UI binding type '%s'",
                                            type != NULL ? type : "<missing>");
                if (!is_non_empty_string(binding, "key"))
                    return validation_error(ctx, binding_path, "UI scene_state binding requires a non-empty key");
            }
        }
    }

    for (size_t i = 0; yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.images[%zu]", i);
        yyjson_val *image = yyjson_arr_get(images, i);
        if (!yyjson_is_obj(image))
            return validation_error(ctx, path, "UI image entries must be objects");
        if (!is_non_empty_string(image, "name"))
            return validation_error(ctx, path, "UI image requires a non-empty name");
        if (!require_ref(ctx, &names->images, "image asset", json_string(image, "image"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(image, "visible_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(rects) && i < yyjson_arr_size(rects); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.rects[%zu]", i);
        yyjson_val *rect = yyjson_arr_get(rects, i);
        if (!yyjson_is_obj(rect))
            return validation_error(ctx, path, "UI rectangle entries must be objects");
        if (!is_non_empty_string(rect, "name"))
            return validation_error(ctx, path, "UI rectangle requires a non-empty name");
        yyjson_val *alpha_source = obj_get(rect, "alpha_source");
        if (alpha_source != NULL)
        {
            if (!yyjson_is_obj(alpha_source))
                return validation_error(ctx, path, "UI rectangle alpha_source must be an object");
            if (!require_ref(ctx, &names->entities, "entity", json_string(alpha_source, "target"), path))
                return false;
            if (!is_non_empty_string(alpha_source, "key"))
                return validation_error(ctx, path, "UI rectangle alpha_source requires a non-empty key");
            yyjson_val *min_value = obj_get(alpha_source, "min");
            yyjson_val *max_value = obj_get(alpha_source, "max");
            if (min_value != NULL && !yyjson_is_num(min_value))
                return validation_error(ctx, path, "UI rectangle alpha_source min must be numeric");
            if (max_value != NULL && !yyjson_is_num(max_value))
                return validation_error(ctx, path, "UI rectangle alpha_source max must be numeric");
            if (yyjson_is_num(min_value) && yyjson_is_num(max_value) &&
                yyjson_get_real(max_value) < yyjson_get_real(min_value))
                return validation_error(ctx, path, "UI rectangle alpha_source max must be >= min");
        }
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(rect, "visible_if"), condition_path, names))
            return false;
    }

    for (size_t i = 0; yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        char path[PATH_BUFFER_SIZE];
        format_path(path, sizeof(path), "$.ui.menus[%zu]", i);
        yyjson_val *menu = yyjson_arr_get(menus, i);
        if (!yyjson_is_obj(menu))
            return validation_error(ctx, path, "UI menu presenters must be objects");
        if (!is_non_empty_string(menu, "name"))
            return validation_error(ctx, path, "UI menu presenter requires a non-empty name");
        if (!is_non_empty_string(menu, "menu"))
            return validation_error(ctx, path, "UI menu presenter requires a menu reference");
        if (!require_ref(ctx, &names->fonts, "font asset", json_string(menu, "font"), path))
            return false;
        yyjson_val *cursor = obj_get(menu, "cursor");
        if (yyjson_is_obj(cursor) && json_string(cursor, "font") != NULL &&
            !require_ref(ctx, &names->fonts, "font asset", json_string(cursor, "font"), path))
            return false;
        char condition_path[PATH_BUFFER_SIZE];
        format_path(condition_path, sizeof(condition_path), "%s.visible_if", path);
        if (!validate_data_condition(ctx, obj_get(menu, "visible_if"), condition_path, names))
            return false;
    }
    return true;
}

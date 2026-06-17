/**
 * @file game_data_conditions_presentation.c
 * @brief Data condition evaluation, UI binding resolution, and presentation clocks.
 */

#include "game_data_internal.h"

int action_signal_id(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key)
{
    return slayer3d_game_data_find_signal(runtime, json_string(action, key, NULL));
}

static bool compare_value(const slayer3d_value *left, const char *op, yyjson_val *right)
{
    if (left == NULL || op == NULL || right == NULL)
        return false;

    if (left->type == SLAYER3D_VALUE_INT && yyjson_is_num(right))
    {
        const int value = (int)yyjson_get_int(right);
        if (SDL_strcmp(op, ">=") == 0)
            return left->as_int >= value;
        if (SDL_strcmp(op, ">") == 0)
            return left->as_int > value;
        if (SDL_strcmp(op, "<=") == 0)
            return left->as_int <= value;
        if (SDL_strcmp(op, "<") == 0)
            return left->as_int < value;
        if (SDL_strcmp(op, "==") == 0)
            return left->as_int == value;
        if (SDL_strcmp(op, "!=") == 0)
            return left->as_int != value;
    }
    if (left->type == SLAYER3D_VALUE_FLOAT && yyjson_is_num(right))
    {
        const float value = (float)yyjson_get_real(right);
        if (SDL_strcmp(op, ">=") == 0)
            return left->as_float >= value;
        if (SDL_strcmp(op, ">") == 0)
            return left->as_float > value;
        if (SDL_strcmp(op, "<=") == 0)
            return left->as_float <= value;
        if (SDL_strcmp(op, "<") == 0)
            return left->as_float < value;
        if (SDL_strcmp(op, "==") == 0)
            return left->as_float == value;
        if (SDL_strcmp(op, "!=") == 0)
            return left->as_float != value;
    }
    if (left->type == SLAYER3D_VALUE_BOOL && yyjson_is_bool(right))
    {
        if (SDL_strcmp(op, "==") == 0)
            return left->as_bool == yyjson_get_bool(right);
        if (SDL_strcmp(op, "!=") == 0)
            return left->as_bool != yyjson_get_bool(right);
    }
    if (left->type == SLAYER3D_VALUE_STRING && yyjson_is_str(right))
    {
        const bool equal = SDL_strcmp(left->as_string, yyjson_get_str(right)) == 0;
        if (SDL_strcmp(op, "==") == 0)
            return equal;
        if (SDL_strcmp(op, "!=") == 0)
            return !equal;
    }
    return false;
}

bool json_scalar_to_value(yyjson_val *json, slayer3d_value *out_value)
{
    if (json == NULL || out_value == NULL)
        return false;

    SDL_zero(*out_value);
    if (yyjson_is_str(json))
    {
        out_value->type = SLAYER3D_VALUE_STRING;
        out_value->as_string = (char *)yyjson_get_str(json);
        return out_value->as_string != NULL;
    }
    if (yyjson_is_bool(json))
    {
        out_value->type = SLAYER3D_VALUE_BOOL;
        out_value->as_bool = yyjson_get_bool(json);
        return true;
    }
    if (yyjson_is_int(json))
    {
        out_value->type = SLAYER3D_VALUE_INT;
        out_value->as_int = (int)yyjson_get_sint(json);
        return true;
    }
    if (yyjson_is_num(json))
    {
        out_value->type = SLAYER3D_VALUE_FLOAT;
        out_value->as_float = (float)yyjson_get_real(json);
        return true;
    }
    return false;
}

bool set_property_from_value(slayer3d_properties *props, const char *key, const slayer3d_value *value)
{
    if (props == NULL || key == NULL || key[0] == '\0' || value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_BOOL:
        slayer3d_properties_set_bool(props, key, value->as_bool);
        return true;
    case SLAYER3D_VALUE_INT:
        slayer3d_properties_set_int(props, key, value->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        slayer3d_properties_set_float(props, key, value->as_float);
        return true;
    case SLAYER3D_VALUE_STRING:
        slayer3d_properties_set_string(props, key, value->as_string != NULL ? value->as_string : "");
        return true;
    case SLAYER3D_VALUE_VEC3:
        slayer3d_properties_set_vec3(props, key, value->as_vec3);
        return true;
    case SLAYER3D_VALUE_COLOR:
        slayer3d_properties_set_color(props, key, value->as_color);
        return true;
    }
    return false;
}

static yyjson_val *find_ui_text_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_texts = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "text");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_texts) && i < yyjson_arr_size(scene_texts); ++i)
    {
        yyjson_val *text = yyjson_arr_get(scene_texts, i);
        const char *text_name = json_string(text, "name", NULL);
        if (text_name != NULL && SDL_strcmp(text_name, name) == 0)
            return text;
    }
    yyjson_val *scene_menus = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "menus");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_menus) && i < yyjson_arr_size(scene_menus); ++i)
    {
        yyjson_val *menu = yyjson_arr_get(scene_menus, i);
        const char *menu_name = json_string(menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return menu;
    }

    yyjson_val *texts = obj_get(obj_get(runtime_root(runtime), "ui"), "text");
    for (size_t i = 0; name != NULL && yyjson_is_arr(texts) && i < yyjson_arr_size(texts); ++i)
    {
        yyjson_val *text = yyjson_arr_get(texts, i);
        const char *text_name = json_string(text, "name", NULL);
        if (text_name != NULL && SDL_strcmp(text_name, name) == 0)
            return text;
    }
    yyjson_val *menus = obj_get(obj_get(runtime_root(runtime), "ui"), "menus");
    for (size_t i = 0; name != NULL && yyjson_is_arr(menus) && i < yyjson_arr_size(menus); ++i)
    {
        yyjson_val *menu = yyjson_arr_get(menus, i);
        const char *menu_name = json_string(menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return menu;
    }
    return NULL;
}

static yyjson_val *find_ui_image_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_images = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "images");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_images) && i < yyjson_arr_size(scene_images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(scene_images, i);
        const char *image_name = json_string(image, "name", NULL);
        if (image_name != NULL && SDL_strcmp(image_name, name) == 0)
            return image;
    }

    yyjson_val *images = obj_get(obj_get(runtime_root(runtime), "ui"), "images");
    for (size_t i = 0; name != NULL && yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(images, i);
        const char *image_name = json_string(image, "name", NULL);
        if (image_name != NULL && SDL_strcmp(image_name, name) == 0)
            return image;
    }
    return NULL;
}

static yyjson_val *find_ui_rect_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    yyjson_val *scene_rects = obj_get(obj_get(scene != NULL ? scene->root : NULL, "ui"), "rects");
    for (size_t i = 0; name != NULL && yyjson_is_arr(scene_rects) && i < yyjson_arr_size(scene_rects); ++i)
    {
        yyjson_val *rect = yyjson_arr_get(scene_rects, i);
        const char *rect_name = json_string(rect, "name", NULL);
        if (rect_name != NULL && SDL_strcmp(rect_name, name) == 0)
            return rect;
    }

    yyjson_val *rects = obj_get(obj_get(runtime_root(runtime), "ui"), "rects");
    for (size_t i = 0; name != NULL && yyjson_is_arr(rects) && i < yyjson_arr_size(rects); ++i)
    {
        yyjson_val *rect = yyjson_arr_get(rects, i);
        const char *rect_name = json_string(rect, "name", NULL);
        if (rect_name != NULL && SDL_strcmp(rect_name, name) == 0)
            return rect;
    }
    return NULL;
}

static bool value_equals_json_bool(const slayer3d_value *left, bool right)
{
    return left != NULL && left->type == SLAYER3D_VALUE_BOOL && left->as_bool == right;
}

static bool eval_data_condition_internal(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                         const slayer3d_game_data_ui_metrics *metrics,
                                         const slayer3d_properties *payload)
{
    if (condition == NULL)
        return true;
    if (!yyjson_is_obj(condition))
        return false;

    const char *type = json_string(condition, "type", "");
    if (SDL_strcmp(type, "always") == 0)
        return true;
    if (SDL_strcmp(type, "camera.active") == 0)
    {
        const char *camera = json_string(condition, "camera", NULL);
        const char *active = slayer3d_game_data_active_camera(runtime);
        return camera != NULL && active != NULL && SDL_strcmp(camera, active) == 0;
    }
    if (SDL_strcmp(type, "app.paused") == 0)
    {
        const bool expected = json_bool(condition, "equals", true);
        return (metrics != NULL && metrics->paused) == expected;
    }
    if (SDL_strcmp(type, "menu.selected") == 0)
    {
        const scene_entry *scene = active_scene_entry_const(runtime);
        const scene_menu_state *menu = find_scene_menu_const(scene, json_string(condition, "menu", NULL));
        return menu != NULL && menu->selected_index == json_int(condition, "index", -1);
    }
    if (SDL_strcmp(type, "property.compare") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(condition, "target", NULL));
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        return target != NULL && key != NULL &&
               compare_value(slayer3d_properties_get_value(target->props, key), op, obj_get(condition, "value"));
    }
    if (SDL_strcmp(type, "scene_state.compare") == 0)
    {
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        const slayer3d_value *left = runtime != NULL && runtime->scene_state != NULL && key != NULL
                                         ? slayer3d_properties_get_value(runtime->scene_state, key)
                                         : NULL;
        slayer3d_value fallback;
        if (left == NULL && json_scalar_to_value(obj_get(condition, "default"), &fallback))
            left = &fallback;
        return key != NULL && compare_value(left, op, obj_get(condition, "value"));
    }
    if (SDL_strcmp(type, "payload.compare") == 0)
    {
        const char *key = json_string(condition, "key", NULL);
        const char *op = json_string(condition, "op", NULL);
        const slayer3d_value *left =
            payload != NULL && key != NULL ? slayer3d_properties_get_value(payload, key) : NULL;
        slayer3d_value fallback;
        if (left == NULL && json_scalar_to_value(obj_get(condition, "default"), &fallback))
            left = &fallback;
        return key != NULL && compare_value(left, op, obj_get(condition, "value"));
    }
    if (SDL_strcmp(type, "property.bool") == 0)
    {
        slayer3d_registered_actor *target =
            slayer3d_game_data_find_actor(runtime, json_string(condition, "target", NULL));
        const char *key = json_string(condition, "key", NULL);
        return target != NULL && key != NULL &&
               value_equals_json_bool(slayer3d_properties_get_value(target->props, key),
                                      json_bool(condition, "equals", true));
    }
    if (SDL_strcmp(type, "all") == 0 || SDL_strcmp(type, "any") == 0)
    {
        yyjson_val *conditions = obj_get(condition, "conditions");
        if (!yyjson_is_arr(conditions))
            return false;
        const bool require_all = SDL_strcmp(type, "all") == 0;
        bool saw_any = false;
        for (size_t i = 0; i < yyjson_arr_size(conditions); ++i)
        {
            saw_any = true;
            const bool passed = eval_data_condition_internal(runtime, yyjson_arr_get(conditions, i), metrics, payload);
            if (require_all && !passed)
                return false;
            if (!require_all && passed)
                return true;
        }
        return require_all && saw_any;
    }
    if (SDL_strcmp(type, "not") == 0)
        return !eval_data_condition_internal(runtime, obj_get(condition, "condition"), metrics, payload);
    return false;
}

bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                         const slayer3d_game_data_ui_metrics *metrics)
{
    return eval_data_condition_internal(runtime, condition, metrics, NULL);
}

bool eval_data_condition_with_payload(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                      const slayer3d_game_data_ui_metrics *metrics, const slayer3d_properties *payload)
{
    return eval_data_condition_internal(runtime, condition, metrics, payload);
}

static bool ui_text_authored_is_visible(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_text *text,
                                        const slayer3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || text == NULL)
        return false;
    yyjson_val *text_json = find_ui_text_json(runtime, text->name);
    yyjson_val *condition = obj_get(text_json, "visible_if");
    if (condition != NULL)
        return eval_data_condition(runtime, condition, metrics);
    return text->visible == NULL || SDL_strcmp(text->visible, "always") == 0;
}

static bool ui_image_authored_is_visible(const slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_ui_image *image,
                                         const slayer3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || image == NULL)
        return false;
    yyjson_val *image_json = find_ui_image_json(runtime, image->name);
    yyjson_val *condition = obj_get(image_json, "visible_if");
    if (condition != NULL)
        return eval_data_condition(runtime, condition, metrics);
    return image->visible == NULL || SDL_strcmp(image->visible, "always") == 0;
}

static bool ui_rect_authored_is_visible(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_rect *rect,
                                        const slayer3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL || rect == NULL)
        return false;
    yyjson_val *rect_json = find_ui_rect_json(runtime, rect->name);
    yyjson_val *condition = obj_get(rect_json, "visible_if");
    if (condition != NULL)
        return eval_data_condition(runtime, condition, metrics);
    return rect->visible == NULL || SDL_strcmp(rect->visible, "always") == 0;
}

static float clamp_unit(float value)
{
    return SDL_clamp(value, 0.0f, 1.0f);
}

static Uint8 multiply_u8(Uint8 value, float multiplier)
{
    return (Uint8)SDL_clamp((int)((float)value * multiplier + 0.5f), 0, 255);
}

static slayer3d_color apply_ui_state_color(slayer3d_color color, const slayer3d_game_data_ui_state *state)
{
    if (state == NULL)
        return color;

    if ((state->flags & SLAYER3D_GAME_DATA_UI_STATE_TINT) != 0u)
    {
        color.r = multiply_u8(color.r, (float)state->tint.r / 255.0f);
        color.g = multiply_u8(color.g, (float)state->tint.g / 255.0f);
        color.b = multiply_u8(color.b, (float)state->tint.b / 255.0f);
        color.a = multiply_u8(color.a, (float)state->tint.a / 255.0f);
    }
    if ((state->flags & SLAYER3D_GAME_DATA_UI_STATE_ALPHA) != 0u)
        color.a = multiply_u8(color.a, clamp_unit(state->alpha));
    return color;
}

bool slayer3d_game_data_resolve_ui_text(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_text *text,
                                        const slayer3d_game_data_ui_metrics *metrics,
                                        slayer3d_game_data_ui_text *out_text, bool *out_visible)
{
    if (out_visible != NULL)
        *out_visible = false;
    if (runtime == NULL || text == NULL || out_text == NULL)
        return false;

    slayer3d_game_data_ui_text resolved = *text;
    bool visible = ui_text_authored_is_visible(runtime, text, metrics);
    slayer3d_game_data_ui_state state;
    if (slayer3d_game_data_get_ui_state(runtime, text->name, &state))
    {
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_VISIBLE) != 0u)
            visible = state.visible;
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_OFFSET) != 0u)
        {
            resolved.x += state.offset_x;
            resolved.y += state.offset_y;
        }
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_SCALE) != 0u)
            resolved.scale *= state.scale;
        resolved.color = apply_ui_state_color(resolved.color, &state);
    }

    if (resolved.scale <= 0.0f || resolved.color.a == 0)
        visible = false;
    if (out_visible != NULL)
        *out_visible = visible;
    *out_text = resolved;
    return true;
}

bool slayer3d_game_data_resolve_ui_image(const slayer3d_game_data_runtime *runtime,
                                         const slayer3d_game_data_ui_image *image,
                                         const slayer3d_game_data_ui_metrics *metrics,
                                         slayer3d_game_data_ui_image *out_image, bool *out_visible)
{
    if (out_visible != NULL)
        *out_visible = false;
    if (runtime == NULL || image == NULL || out_image == NULL)
        return false;

    slayer3d_game_data_ui_image resolved = *image;
    bool visible = ui_image_authored_is_visible(runtime, image, metrics);
    if (image->scroll_y_key != NULL && image->scroll_y_key[0] != '\0')
        resolved.y -= slayer3d_properties_get_float(runtime->scene_state, image->scroll_y_key, 0.0f);
    slayer3d_game_data_ui_state state;
    if (slayer3d_game_data_get_ui_state(runtime, image->name, &state))
    {
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_VISIBLE) != 0u)
            visible = state.visible;
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_OFFSET) != 0u)
        {
            resolved.x += state.offset_x;
            resolved.y += state.offset_y;
        }
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_SCALE) != 0u)
            resolved.scale *= state.scale;
        resolved.color = apply_ui_state_color(resolved.color, &state);
    }

    if (resolved.scale <= 0.0f || resolved.color.a == 0)
        visible = false;
    if (out_visible != NULL)
        *out_visible = visible;
    *out_image = resolved;
    return true;
}

bool slayer3d_game_data_resolve_ui_rect(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_game_data_ui_rect *rect,
                                        const slayer3d_game_data_ui_metrics *metrics,
                                        slayer3d_game_data_ui_rect *out_rect, bool *out_visible)
{
    if (out_visible != NULL)
        *out_visible = false;
    if (runtime == NULL || rect == NULL || out_rect == NULL)
        return false;

    slayer3d_game_data_ui_rect resolved = *rect;
    bool visible = ui_rect_authored_is_visible(runtime, rect, metrics);

    if (rect->alpha_source_target != NULL && rect->alpha_source_key != NULL)
    {
        const slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, rect->alpha_source_target);
        const slayer3d_value *value =
            actor != NULL ? slayer3d_properties_get_value(actor->props, rect->alpha_source_key) : NULL;
        float source = 0.0f;
        if (value != NULL && value->type == SLAYER3D_VALUE_FLOAT)
            source = value->as_float;
        else if (value != NULL && value->type == SLAYER3D_VALUE_INT)
            source = (float)value->as_int;
        else
            visible = false;

        const float alpha_scale =
            SDL_clamp(source * rect->alpha_source_scale, rect->alpha_source_min, rect->alpha_source_max);
        resolved.color.a = multiply_u8(resolved.color.a, alpha_scale);
    }

    slayer3d_game_data_ui_state state;
    if (slayer3d_game_data_get_ui_state(runtime, rect->name, &state))
    {
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_VISIBLE) != 0u)
            visible = state.visible;
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_OFFSET) != 0u)
        {
            resolved.x += state.offset_x;
            resolved.y += state.offset_y;
        }
        if ((state.flags & SLAYER3D_GAME_DATA_UI_STATE_SCALE) != 0u)
            resolved.scale *= state.scale;
        resolved.color = apply_ui_state_color(resolved.color, &state);
    }

    if (resolved.scale <= 0.0f || resolved.w <= 0.0f || resolved.h <= 0.0f || resolved.color.a == 0)
        visible = false;
    if (out_visible != NULL)
        *out_visible = visible;
    *out_rect = resolved;
    return true;
}

bool slayer3d_game_data_ui_text_is_visible(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_ui_text *text,
                                           const slayer3d_game_data_ui_metrics *metrics)
{
    bool visible = false;
    slayer3d_game_data_ui_text resolved;
    return slayer3d_game_data_resolve_ui_text(runtime, text, metrics, &resolved, &visible) && visible;
}

bool slayer3d_game_data_ui_image_is_visible(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_game_data_ui_image *image,
                                            const slayer3d_game_data_ui_metrics *metrics)
{
    bool visible = false;
    slayer3d_game_data_ui_image resolved;
    return slayer3d_game_data_resolve_ui_image(runtime, image, metrics, &resolved, &visible) && visible;
}

bool slayer3d_game_data_ui_rect_is_visible(const slayer3d_game_data_runtime *runtime,
                                           const slayer3d_game_data_ui_rect *rect,
                                           const slayer3d_game_data_ui_metrics *metrics)
{
    bool visible = false;
    slayer3d_game_data_ui_rect resolved;
    return slayer3d_game_data_resolve_ui_rect(runtime, rect, metrics, &resolved, &visible) && visible;
}

bool slayer3d_game_data_app_pause_allowed(const slayer3d_game_data_runtime *runtime,
                                          const slayer3d_game_data_ui_metrics *metrics)
{
    if (runtime == NULL)
        return false;

    yyjson_val *pause = obj_get(obj_get(runtime_root(runtime), "app"), "pause");
    yyjson_val *condition = obj_get(pause, "allowed_if");
    if (condition == NULL)
        return true;
    return eval_data_condition(runtime, condition, metrics);
}

static yyjson_val *find_presentation_clock(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *clocks = obj_get(obj_get(runtime_root(runtime), "presentation"), "clocks");
    for (size_t i = 0; name != NULL && yyjson_is_arr(clocks) && i < yyjson_arr_size(clocks); ++i)
    {
        yyjson_val *clock = yyjson_arr_get(clocks, i);
        const char *clock_name = json_string(clock, "name", NULL);
        if (clock_name != NULL && SDL_strcmp(clock_name, name) == 0)
            return clock;
    }
    return NULL;
}

static slayer3d_registered_actor *clock_actor(const slayer3d_game_data_runtime *runtime, yyjson_val *clock)
{
    return slayer3d_game_data_find_actor(runtime, json_string(clock, "target", NULL));
}

static float clock_property_float(const slayer3d_game_data_runtime *runtime, yyjson_val *ref, float fallback)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(ref, "target", NULL));
    const char *key = json_string(ref, "key", NULL);
    return actor != NULL && key != NULL ? slayer3d_properties_get_float(actor->props, key, fallback) : fallback;
}

bool slayer3d_game_data_update_presentation_clocks(slayer3d_game_data_runtime *runtime, float dt, bool paused,
                                                   bool pause_entered)
{
    if (runtime == NULL)
        return false;
    yyjson_val *clocks = obj_get(obj_get(runtime_root(runtime), "presentation"), "clocks");
    if (!yyjson_is_arr(clocks))
        return true;

    slayer3d_game_data_ui_metrics metrics;
    SDL_zero(metrics);
    metrics.paused = paused;
    for (size_t i = 0; i < yyjson_arr_size(clocks); ++i)
    {
        yyjson_val *clock = yyjson_arr_get(clocks, i);
        slayer3d_registered_actor *actor = clock_actor(runtime, clock);
        const char *key = json_string(clock, "key", NULL);
        if (actor == NULL || key == NULL)
            continue;

        float value = slayer3d_properties_get_float(actor->props, key, json_float(clock, "initial", 0.0f));
        if (pause_entered && json_bool(clock, "reset_on_pause_enter", false))
            value = json_float(clock, "reset_value", 0.0f);

        yyjson_val *condition = obj_get(clock, "active_if");
        const bool active = condition == NULL || eval_data_condition(runtime, condition, &metrics);
        if (active)
        {
            const float speed =
                clock_property_float(runtime, obj_get(clock, "speed_property"), json_float(clock, "speed", 1.0f));
            value += dt * speed;
            const float wrap = json_float(clock, "wrap", 0.0f);
            if (wrap > 0.0f)
            {
                while (value >= wrap)
                    value -= wrap;
                while (value < 0.0f)
                    value += wrap;
            }
        }
        slayer3d_properties_set_float(actor->props, key, value);
    }
    return true;
}

float slayer3d_game_data_ui_pulse_phase(const slayer3d_game_data_runtime *runtime, float fallback)
{
    if (runtime == NULL)
        return fallback;
    const char *clock_name = json_string(obj_get(runtime_root(runtime), "presentation"), "ui_pulse_clock", NULL);
    yyjson_val *clock = find_presentation_clock(runtime, clock_name);
    slayer3d_registered_actor *actor = clock_actor(runtime, clock);
    const char *key = json_string(clock, "key", NULL);
    return actor != NULL && key != NULL ? slayer3d_properties_get_float(actor->props, key, fallback) : fallback;
}

float slayer3d_game_data_fps_sample_seconds(const slayer3d_game_data_runtime *runtime, float fallback)
{
    if (runtime == NULL)
        return fallback;
    const float sample_seconds =
        json_float(obj_get(obj_get(runtime_root(runtime), "presentation"), "metrics"), "fps_sample_seconds", fallback);
    return sample_seconds > 0.0f ? sample_seconds : fallback;
}

typedef enum ui_value_type
{
    UI_VALUE_NONE,
    UI_VALUE_INT,
    UI_VALUE_FLOAT,
    UI_VALUE_UINT64,
    UI_VALUE_BOOL,
    UI_VALUE_STRING,
} ui_value_type;

typedef struct ui_value
{
    ui_value_type type;
    union {
        int as_int;
        float as_float;
        Uint64 as_uint64;
        bool as_bool;
        const char *as_string;
    };
} ui_value;

static bool read_brush_diagnostic_metric(const slayer3d_game_data_runtime *runtime, const char *metric,
                                         ui_value *out_value)
{
    if (runtime == NULL || metric == NULL || out_value == NULL || SDL_strncmp(metric, "brush.", 6) != 0)
        return false;

    slayer3d_game_data_brush_diagnostics diagnostics;
    if (!slayer3d_game_data_get_brush_diagnostics(runtime, &diagnostics))
        return false;

    const char *name = metric + 6;
    Uint64 value = 0u;
    if (SDL_strcmp(name, "trace_count") == 0)
        value = diagnostics.trace_count;
    else if (SDL_strcmp(name, "world_instance_count") == 0)
        value = diagnostics.world_instance_count;
    else if (SDL_strcmp(name, "world_bounds_reject_count") == 0)
        value = diagnostics.world_bounds_reject_count;
    else if (SDL_strcmp(name, "brush_count") == 0)
        value = diagnostics.brush_count;
    else if (SDL_strcmp(name, "contents_reject_count") == 0)
        value = diagnostics.contents_reject_count;
    else if (SDL_strcmp(name, "bounds_reject_count") == 0)
        value = diagnostics.bounds_reject_count;
    else if (SDL_strcmp(name, "collision_candidate_count") == 0)
        value = diagnostics.collision_candidate_count;
    else if (SDL_strcmp(name, "hit_count") == 0)
        value = diagnostics.hit_count;
    else if (SDL_strcmp(name, "render_mesh_submissions") == 0)
        value = diagnostics.render_mesh_submissions;
    else if (SDL_strcmp(name, "render_mesh_culled") == 0)
        value = diagnostics.render_mesh_culled;
    else if (SDL_strcmp(name, "render_mesh_draws") == 0)
        value = diagnostics.render_mesh_draws;
    else if (SDL_strcmp(name, "render_triangles_submitted") == 0)
        value = diagnostics.render_triangles_submitted;
    else if (SDL_strcmp(name, "compile_face_count") == 0)
        value = diagnostics.compile_face_count;
    else if (SDL_strcmp(name, "compile_rendered_face_count") == 0)
        value = diagnostics.compile_rendered_face_count;
    else if (SDL_strcmp(name, "compile_culled_face_count") == 0)
        value = diagnostics.compile_culled_face_count;
    else if (SDL_strcmp(name, "compile_triangle_count") == 0)
        value = diagnostics.compile_triangle_count;
    else if (SDL_strcmp(name, "compile_invalid_brush_count") == 0)
        value = diagnostics.compile_invalid_brush_count;
    else if (SDL_strcmp(name, "compile_degenerate_face_count") == 0)
        value = diagnostics.compile_degenerate_face_count;
    else if (SDL_strcmp(name, "compile_chunk_count") == 0)
        value = diagnostics.compile_chunk_count;
    else if (SDL_strcmp(name, "collision_chunk_count") == 0)
        value = diagnostics.collision_chunk_count;
    else if (SDL_strcmp(name, "collision_chunk_reject_count") == 0)
        value = diagnostics.collision_chunk_reject_count;
    else if (SDL_strcmp(name, "render_chunk_draws") == 0)
        value = diagnostics.render_chunk_draws;
    else if (SDL_strcmp(name, "render_chunk_brushes_drawn") == 0)
        value = diagnostics.render_chunk_brushes_drawn;
    else if (SDL_strcmp(name, "frustum_brush_candidates") == 0)
        value = diagnostics.frustum_brush_candidates;
    else if (SDL_strcmp(name, "frustum_brush_culled") == 0)
        value = diagnostics.frustum_brush_culled;
    else if (SDL_strcmp(name, "frustum_triangles_culled") == 0)
        value = diagnostics.frustum_triangles_culled;
    else if (SDL_strcmp(name, "visibility_brush_candidates") == 0)
        value = diagnostics.visibility_brush_candidates;
    else if (SDL_strcmp(name, "visibility_brush_visible") == 0)
        value = diagnostics.visibility_brush_visible;
    else if (SDL_strcmp(name, "visibility_brush_occluded") == 0)
        value = diagnostics.visibility_brush_occluded;
    else if (SDL_strcmp(name, "visibility_triangles_culled") == 0)
        value = diagnostics.visibility_triangles_culled;
    else if (SDL_strcmp(name, "visibility_grid_cache_hits") == 0)
        value = diagnostics.visibility_grid_cache_hits;
    else if (SDL_strcmp(name, "visibility_grid_cache_misses") == 0)
        value = diagnostics.visibility_grid_cache_misses;
    else
        return false;

    out_value->type = UI_VALUE_UINT64;
    out_value->as_uint64 = value;
    return true;
}

static bool read_performance_metric(const char *metric, const slayer3d_game_data_ui_metrics *metrics,
                                    ui_value *out_value)
{
    if (metric == NULL || out_value == NULL)
        return false;

    if (SDL_strcmp(metric, "perf.frame_ms") == 0)
        out_value->as_float = metrics != NULL ? metrics->frame_ms : 0.0f;
    else if (SDL_strcmp(metric, "perf.update_cpu_ms") == 0)
        out_value->as_float = metrics != NULL ? metrics->update_cpu_ms : 0.0f;
    else if (SDL_strcmp(metric, "perf.render_cpu_ms") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_cpu_ms : 0.0f;
    else if (SDL_strcmp(metric, "render.model_mesh_submissions_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_mesh_submissions_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.model_mesh_draws_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_mesh_draws_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.model_triangles_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_triangles_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.geometry_draw_calls_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_geometry_draw_calls_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.static_mesh_instanced_draw_calls_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_static_mesh_instanced_draw_calls_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.static_mesh_instances_batched_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_static_mesh_instances_batched_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.static_mesh_draw_calls_saved_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_static_mesh_draw_calls_saved_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.procedural_lod_candidates_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_procedural_lod_candidates_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.procedural_lod_reduced_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_procedural_lod_reduced_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.procedural_lod_authored_triangles_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_procedural_lod_authored_triangles_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.procedural_lod_resolved_triangles_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_procedural_lod_resolved_triangles_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.procedural_lod_triangles_saved_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_procedural_lod_triangles_saved_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.model_lod_candidates_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_lod_candidates_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.model_lod_culled_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_lod_culled_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.model_lod_triangles_saved_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_model_lod_triangles_saved_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.depth_prepass_draws_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_depth_prepass_draws_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.depth_prepass_triangles_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_depth_prepass_triangles_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.depth_prepass_samples_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_depth_prepass_samples_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.geometry_samples_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_geometry_samples_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.light_candidates_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_light_candidates_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.lights_selected_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_lights_selected_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.light_selection_draws_per_frame") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_light_selection_draws_per_frame : 0.0f;
    else if (SDL_strcmp(metric, "render.light_selection_ratio") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_light_selection_ratio : 0.0f;
    else if (SDL_strcmp(metric, "render.world_scale") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_world_scale : 1.0f;
    else if (SDL_strcmp(metric, "render.world_width") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_world_width : 0.0f;
    else if (SDL_strcmp(metric, "render.world_height") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_world_height : 0.0f;
    else if (SDL_strcmp(metric, "render.window_pixel_width") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_window_pixel_width : 0.0f;
    else if (SDL_strcmp(metric, "render.window_pixel_height") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_window_pixel_height : 0.0f;
    else if (SDL_strcmp(metric, "render.window_pixel_density") == 0)
        out_value->as_float = metrics != NULL ? metrics->render_window_pixel_density : 1.0f;
    else
        return false;

    out_value->type = UI_VALUE_FLOAT;
    return true;
}

static bool read_ui_binding_value(const slayer3d_game_data_runtime *runtime, yyjson_val *binding,
                                  const slayer3d_game_data_ui_metrics *metrics, ui_value *out_value)
{
    if (out_value != NULL)
        SDL_zero(*out_value);
    if (binding == NULL || out_value == NULL)
        return false;

    const char *type = json_string(binding, "type", "");
    if (SDL_strcmp(type, "metric") == 0)
    {
        const char *metric = json_string(binding, "metric", NULL);
        if (metric != NULL && SDL_strcmp(metric, "fps") == 0)
        {
            out_value->type = UI_VALUE_FLOAT;
            out_value->as_float = metrics != NULL ? metrics->fps : 0.0f;
            return true;
        }
        if (metric != NULL && SDL_strcmp(metric, "frame") == 0)
        {
            out_value->type = UI_VALUE_UINT64;
            out_value->as_uint64 = metrics != NULL ? metrics->frame : 0u;
            return true;
        }
        if (metric != NULL && SDL_strcmp(metric, "paused") == 0)
        {
            out_value->type = UI_VALUE_BOOL;
            out_value->as_bool = metrics != NULL && metrics->paused;
            return true;
        }
        if (read_performance_metric(metric, metrics, out_value))
            return true;
        if (read_brush_diagnostic_metric(runtime, metric, out_value))
            return true;
        return false;
    }

    if (SDL_strcmp(type, "property") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(binding, "entity", NULL));
        const char *key = json_string(binding, "key", NULL);
        const slayer3d_value *value =
            actor != NULL && key != NULL ? slayer3d_properties_get_value(actor->props, key) : NULL;
        if (value == NULL)
            return false;
        switch (value->type)
        {
        case SLAYER3D_VALUE_INT:
            out_value->type = UI_VALUE_INT;
            out_value->as_int = value->as_int;
            return true;
        case SLAYER3D_VALUE_FLOAT:
            out_value->type = UI_VALUE_FLOAT;
            out_value->as_float = value->as_float;
            return true;
        case SLAYER3D_VALUE_BOOL:
            out_value->type = UI_VALUE_BOOL;
            out_value->as_bool = value->as_bool;
            return true;
        case SLAYER3D_VALUE_STRING:
            out_value->type = UI_VALUE_STRING;
            out_value->as_string = value->as_string != NULL ? value->as_string : "";
            return true;
        case SLAYER3D_VALUE_VEC3:
        case SLAYER3D_VALUE_COLOR:
            return false;
        }
    }
    if (SDL_strcmp(type, "scene_state") == 0)
    {
        const char *key = json_string(binding, "key", NULL);
        const slayer3d_value *value = runtime != NULL && runtime->scene_state != NULL && key != NULL
                                          ? slayer3d_properties_get_value(runtime->scene_state, key)
                                          : NULL;
        slayer3d_value fallback;
        if (value == NULL && json_scalar_to_value(obj_get(binding, "default"), &fallback))
            value = &fallback;
        if (value == NULL)
            return false;
        switch (value->type)
        {
        case SLAYER3D_VALUE_INT:
            out_value->type = UI_VALUE_INT;
            out_value->as_int = value->as_int;
            return true;
        case SLAYER3D_VALUE_FLOAT:
            out_value->type = UI_VALUE_FLOAT;
            out_value->as_float = value->as_float;
            return true;
        case SLAYER3D_VALUE_BOOL:
            out_value->type = UI_VALUE_BOOL;
            out_value->as_bool = value->as_bool;
            return true;
        case SLAYER3D_VALUE_STRING:
            out_value->type = UI_VALUE_STRING;
            out_value->as_string = value->as_string != NULL ? value->as_string : "";
            return true;
        case SLAYER3D_VALUE_VEC3:
        case SLAYER3D_VALUE_COLOR:
            return false;
        }
    }
    return false;
}

static bool format_bound_ui_text(const char *format, const ui_value *values, int value_count, char *buffer,
                                 size_t buffer_size)
{
    if (format == NULL || values == NULL || buffer == NULL || buffer_size == 0)
        return false;

    if (value_count == 1)
    {
        if (values[0].type == UI_VALUE_INT)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_int);
        else if (values[0].type == UI_VALUE_FLOAT)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_float);
        else if (values[0].type == UI_VALUE_UINT64)
            SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64);
        else if (values[0].type == UI_VALUE_BOOL)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_bool ? 1 : 0);
        else if (values[0].type == UI_VALUE_STRING)
            SDL_snprintf(buffer, buffer_size, format, values[0].as_string);
        else
            return false;
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_INT && values[1].type == UI_VALUE_INT)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_int, values[1].as_int);
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_FLOAT && values[1].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_float, (unsigned long long)values[1].as_uint64);
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_FLOAT && values[1].type == UI_VALUE_FLOAT)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_float, values[1].as_float);
        return true;
    }
    if (value_count == 2 && values[0].type == UI_VALUE_UINT64 && values[1].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64,
                     (unsigned long long)values[1].as_uint64);
        return true;
    }
    if (value_count == 3 && values[0].type == UI_VALUE_UINT64 && values[1].type == UI_VALUE_UINT64 &&
        values[2].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64,
                     (unsigned long long)values[1].as_uint64, (unsigned long long)values[2].as_uint64);
        return true;
    }
    if (value_count == 3 && values[0].type == UI_VALUE_FLOAT && values[1].type == UI_VALUE_FLOAT &&
        values[2].type == UI_VALUE_FLOAT)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_float, values[1].as_float, values[2].as_float);
        return true;
    }
    if (value_count == 4 && values[0].type == UI_VALUE_UINT64 && values[1].type == UI_VALUE_UINT64 &&
        values[2].type == UI_VALUE_UINT64 && values[3].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64,
                     (unsigned long long)values[1].as_uint64, (unsigned long long)values[2].as_uint64,
                     (unsigned long long)values[3].as_uint64);
        return true;
    }
    if (value_count == 4 && values[0].type == UI_VALUE_FLOAT && values[1].type == UI_VALUE_FLOAT &&
        values[2].type == UI_VALUE_FLOAT && values[3].type == UI_VALUE_FLOAT)
    {
        SDL_snprintf(buffer, buffer_size, format, values[0].as_float, values[1].as_float, values[2].as_float,
                     values[3].as_float);
        return true;
    }
    return false;
}

bool slayer3d_game_data_format_ui_text(const slayer3d_game_data_runtime *runtime,
                                       const slayer3d_game_data_ui_text *text,
                                       const slayer3d_game_data_ui_metrics *metrics, char *buffer, size_t buffer_size)
{
    if (buffer != NULL && buffer_size > 0)
        buffer[0] = '\0';
    if (runtime == NULL || text == NULL || buffer == NULL || buffer_size == 0)
        return false;

    yyjson_val *text_json = find_ui_text_json(runtime, text->name);
    yyjson_val *bindings = obj_get(text_json, "bindings");
    if (yyjson_is_arr(bindings) && yyjson_arr_size(bindings) > 0)
    {
        ui_value values[4];
        const int value_count = (int)SDL_min(yyjson_arr_size(bindings), SDL_arraysize(values));
        for (int i = 0; i < value_count; ++i)
        {
            if (!read_ui_binding_value(runtime, yyjson_arr_get(bindings, (size_t)i), metrics, &values[i]))
                return false;
        }
        return format_bound_ui_text(text->format, values, value_count, buffer, buffer_size);
    }

    if (text->text != NULL)
    {
        SDL_snprintf(buffer, buffer_size, "%s", text->text);
        return true;
    }
    return false;
}

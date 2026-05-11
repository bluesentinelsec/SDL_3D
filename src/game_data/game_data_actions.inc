/* Data action execution and presentation helpers.
 * Included by src/game_data.c to keep private runtime helpers in one translation unit. */

static int action_signal_id(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key)
{
    return slayer3d_game_data_find_signal(runtime, json_string(action, key, NULL));
}

static bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                 const slayer3d_properties *payload);
static bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                          const slayer3d_properties *payload);
static int actor_sector_index_for_sensor(const sector_level_runtime *level, const sensor_entry *sensor,
                                         const slayer3d_registered_actor *actor);
static float sector_door_distance_sq_xz(const slayer3d_door *door, slayer3d_vec3 point);
static bool sector_door_is_in_front(const slayer3d_door *door, slayer3d_vec3 point, float yaw, float min_dot);

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
    }
    if (left->type == SLAYER3D_VALUE_BOOL && yyjson_is_bool(right) && SDL_strcmp(op, "==") == 0)
        return left->as_bool == yyjson_get_bool(right);
    if (left->type == SLAYER3D_VALUE_STRING && yyjson_is_str(right) && SDL_strcmp(op, "==") == 0)
        return SDL_strcmp(left->as_string, yyjson_get_str(right)) == 0;
    return false;
}

static bool json_scalar_to_value(yyjson_val *json, slayer3d_value *out_value)
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

static bool set_property_from_value(slayer3d_properties *props, const char *key, const slayer3d_value *value)
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
        return false;
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

static bool eval_data_condition(const slayer3d_game_data_runtime *runtime, yyjson_val *condition,
                                const slayer3d_game_data_ui_metrics *metrics)
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
            const bool passed = eval_data_condition(runtime, yyjson_arr_get(conditions, i), metrics);
            if (require_all && !passed)
                return false;
            if (!require_all && passed)
                return true;
        }
        return require_all && saw_any;
    }
    if (SDL_strcmp(type, "not") == 0)
        return !eval_data_condition(runtime, obj_get(condition, "condition"), metrics);
    return false;
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
    return json_float(obj_get(obj_get(runtime_root(runtime), "presentation"), "metrics"), "fps_sample_seconds",
                      fallback);
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
    else
        return false;

    out_value->type = UI_VALUE_UINT64;
    out_value->as_uint64 = value;
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
    if (value_count == 4 && values[0].type == UI_VALUE_UINT64 && values[1].type == UI_VALUE_UINT64 &&
        values[2].type == UI_VALUE_UINT64 && values[3].type == UI_VALUE_UINT64)
    {
        SDL_snprintf(buffer, buffer_size, format, (unsigned long long)values[0].as_uint64,
                     (unsigned long long)values[1].as_uint64, (unsigned long long)values[2].as_uint64,
                     (unsigned long long)values[3].as_uint64);
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

static slayer3d_value_type property_type_from_name(const char *name, slayer3d_value_type fallback)
{
    if (name == NULL)
        return fallback;
    if (SDL_strcmp(name, "int") == 0)
        return SLAYER3D_VALUE_INT;
    if (SDL_strcmp(name, "float") == 0 || SDL_strcmp(name, "number") == 0)
        return SLAYER3D_VALUE_FLOAT;
    if (SDL_strcmp(name, "vec3") == 0)
        return SLAYER3D_VALUE_VEC3;
    if (SDL_strcmp(name, "color") == 0)
        return SLAYER3D_VALUE_COLOR;
    return fallback;
}

static bool start_property_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
    const char *key = json_string(action, "key", NULL);
    yyjson_val *to_json = obj_get(action, "to");
    if (to_json == NULL)
        to_json = obj_get(action, "value");
    if (actor == NULL || key == NULL || to_json == NULL)
        return false;

    const slayer3d_value *current = slayer3d_properties_get_value(actor->props, key);
    const char *value_type = json_string(action, "value_type", NULL);
    const game_data_tween_value_type preferred = tween_preferred_type(value_type, current, NULL);

    game_data_animation animation;
    SDL_zero(animation);
    animation.target_type = GAME_DATA_TWEEN_PROPERTY;
    animation.target = actor->name;
    animation.key = key;
    animation.scene = slayer3d_game_data_active_scene(runtime);
    animation.duration = json_float(action, "duration", 0.0f);
    animation.easing = parse_tween_easing(json_string(action, "easing", NULL));
    animation.repeat = parse_tween_repeat(json_string(action, "repeat", NULL));
    animation.done_signal_id = action_signal_id(runtime, action, "done_signal");
    animation.property_type =
        current != NULL ? current->type : property_type_from_name(value_type, SLAYER3D_VALUE_FLOAT);

    yyjson_val *from_json = obj_get(action, "from");
    if (from_json != NULL)
    {
        if (!json_tween_value(from_json, preferred, &animation.from))
            return false;
    }
    else if (!current_property_tween_value(current, preferred, &animation.from, &animation.property_type))
    {
        animation.from = preferred == GAME_DATA_TWEEN_COLOR  ? tween_color((slayer3d_color){255, 255, 255, 255})
                         : preferred == GAME_DATA_TWEEN_VEC3 ? tween_vec3(slayer3d_vec3_make(0.0f, 0.0f, 0.0f))
                                                             : tween_float(0.0f);
    }

    if (!json_tween_value(to_json, preferred, &animation.to))
        return false;
    if (animation.from.type != animation.to.type)
        return false;
    if (animation.to.type == GAME_DATA_TWEEN_VEC3)
        animation.property_type = SLAYER3D_VALUE_VEC3;
    else if (animation.to.type == GAME_DATA_TWEEN_COLOR)
        animation.property_type = SLAYER3D_VALUE_COLOR;
    else
        animation.property_type = property_type_from_name(value_type, animation.property_type);

    return start_animation(runtime, &animation);
}

static bool start_ui_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", json_string(action, "ui", NULL));
    const char *property = json_string(action, "property", NULL);
    yyjson_val *to_json = obj_get(action, "to");
    if (to_json == NULL)
        to_json = obj_get(action, "value");
    if (target == NULL || property == NULL || to_json == NULL)
        return false;

    slayer3d_game_data_ui_state state;
    slayer3d_game_data_ui_state_init(&state);
    (void)slayer3d_game_data_get_ui_state(runtime, target, &state);
    const game_data_tween_value_type preferred =
        tween_preferred_type(json_string(action, "value_type", NULL), NULL, property);

    game_data_animation animation;
    SDL_zero(animation);
    animation.target_type = GAME_DATA_TWEEN_UI;
    animation.target = target;
    animation.property = property;
    animation.scene = slayer3d_game_data_active_scene(runtime);
    animation.duration = json_float(action, "duration", 0.0f);
    animation.easing = parse_tween_easing(json_string(action, "easing", NULL));
    animation.repeat = parse_tween_repeat(json_string(action, "repeat", NULL));
    animation.done_signal_id = action_signal_id(runtime, action, "done_signal");
    animation.property_type = SLAYER3D_VALUE_FLOAT;

    yyjson_val *from_json = obj_get(action, "from");
    if (from_json != NULL)
    {
        if (!json_tween_value(from_json, preferred, &animation.from))
            return false;
    }
    else if (!current_ui_tween_value(&state, property, &animation.from))
    {
        return false;
    }

    if (!json_tween_value(to_json, preferred, &animation.to))
        return false;
    if (animation.from.type != animation.to.type)
        return false;
    return start_animation(runtime, &animation);
}

bool slayer3d_game_data_update_animations(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL)
        return false;

    reset_animation_scope_if_needed(runtime);
    const float step = dt > 0.0f ? dt : 0.0f;
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    for (int i = 0; i < runtime->animation_count;)
    {
        game_data_animation *animation = &runtime->animations[i];
        animation->elapsed += step;

        bool complete = false;
        float t = 1.0f;
        if (animation->duration > 0.0f)
        {
            if (animation->repeat == GAME_DATA_TWEEN_REPEAT_LOOP)
            {
                t = SDL_fmodf(animation->elapsed, animation->duration) / animation->duration;
            }
            else if (animation->repeat == GAME_DATA_TWEEN_REPEAT_PING_PONG)
            {
                const float cycle_float = SDL_floorf(animation->elapsed / animation->duration);
                t = SDL_fmodf(animation->elapsed, animation->duration) / animation->duration;
                if (((int)cycle_float % 2) != 0)
                    t = 1.0f - t;
            }
            else
            {
                complete = animation->elapsed >= animation->duration;
                t = complete ? 1.0f : animation->elapsed / animation->duration;
            }
        }

        const float eased = apply_tween_easing(animation->easing, t);
        const game_data_tween_value value = interpolate_tween_value(&animation->from, &animation->to, eased);
        apply_animation_value(runtime, animation, &value);

        if (complete)
        {
            if (animation->done_signal_id >= 0 && bus != NULL)
                slayer3d_signal_emit(bus, animation->done_signal_id, NULL);
            runtime->animations[i] = runtime->animations[runtime->animation_count - 1];
            --runtime->animation_count;
            continue;
        }
        ++i;
    }
    return true;
}

static bool ensure_audio_file_capacity(slayer3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->audio_file_capacity)
        return true;

    int next_capacity = runtime->audio_file_capacity < 8 ? 8 : runtime->audio_file_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    materialized_audio_file *files =
        (materialized_audio_file *)SDL_realloc(runtime->audio_files, (size_t)next_capacity * sizeof(*files));
    if (files == NULL)
        return false;

    SDL_memset(files + runtime->audio_file_capacity, 0,
               (size_t)(next_capacity - runtime->audio_file_capacity) * sizeof(*files));
    runtime->audio_files = files;
    runtime->audio_file_capacity = next_capacity;
    return true;
}

static uint32_t hash_audio_asset_path(const char *path)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)path; p != NULL && *p != '\0'; ++p)
    {
        hash ^= (uint32_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

static char *make_safe_audio_filename(const char *path)
{
    char *base = path_basename(asset_path_without_scheme(path));
    if (base == NULL)
        return NULL;

    for (char *p = base; *p != '\0'; ++p)
    {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' ||
              *p == '_' || *p == '-'))
        {
            *p = '_';
        }
    }

    char hash[16];
    SDL_snprintf(hash, sizeof(hash), "%08x-", (unsigned int)hash_audio_asset_path(path));
    const size_t hash_len = SDL_strlen(hash);
    const size_t base_len = SDL_strlen(base);
    char *filename = (char *)SDL_malloc(hash_len + base_len + 1u);
    if (filename == NULL)
    {
        SDL_free(base);
        return NULL;
    }
    SDL_memcpy(filename, hash, hash_len);
    SDL_memcpy(filename + hash_len, base, base_len + 1u);
    SDL_free(base);
    return filename;
}

static bool ensure_runtime_storage(slayer3d_game_data_runtime *runtime, char *error_buffer, int error_buffer_size)
{
    if (runtime == NULL)
    {
        set_error(error_buffer, error_buffer_size, "game data runtime is required");
        return false;
    }
    if (runtime->storage != NULL)
        return true;

    return slayer3d_storage_create(&runtime->storage_config, &runtime->storage, error_buffer, error_buffer_size);
}

static yyjson_val *find_persistence_entry(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *entries = obj_get(obj_get(runtime_root(runtime), "persistence"), "entries");
    for (size_t i = 0; name != NULL && yyjson_is_arr(entries) && i < yyjson_arr_size(entries); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(entries, i);
        const char *entry_name = json_string(entry, "name", NULL);
        if (entry_name != NULL && SDL_strcmp(entry_name, name) == 0)
            return entry;
    }
    return NULL;
}

static const char *action_persistence_entry_name(yyjson_val *action)
{
    const char *entry = json_string(action, "entry", NULL);
    return entry != NULL ? entry : json_string(action, "name", NULL);
}

static bool persistence_entry_is_enabled(slayer3d_game_data_runtime *runtime, yyjson_val *entry)
{
    yyjson_val *condition = obj_get(entry, "enabled_if");
    return condition == NULL || eval_data_condition(runtime, condition, NULL);
}

static const char *persistence_property_key(yyjson_val *property)
{
    if (yyjson_is_str(property))
        return yyjson_get_str(property);
    return json_string(property, "key", NULL);
}

static slayer3d_registered_actor *action_target_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    const char *target_from_payload = json_string(action, "target_from_payload", NULL);
    if (target_from_payload != NULL)
        return actor_from_payload_key(runtime, payload, target_from_payload);
    return slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
}

static bool json_add_property_value(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key,
                                    const slayer3d_value *value)
{
    if (doc == NULL || object == NULL || key == NULL || value == NULL)
        return false;

    switch (value->type)
    {
    case SLAYER3D_VALUE_INT:
        return yyjson_mut_obj_add_int(doc, object, key, value->as_int);
    case SLAYER3D_VALUE_FLOAT:
        return yyjson_mut_obj_add_real(doc, object, key, value->as_float);
    case SLAYER3D_VALUE_BOOL:
        return yyjson_mut_obj_add_bool(doc, object, key, value->as_bool);
    case SLAYER3D_VALUE_STRING:
        return yyjson_mut_obj_add_strcpy(doc, object, key, value->as_string != NULL ? value->as_string : "");
    case SLAYER3D_VALUE_VEC3: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_real(doc, arr, value->as_vec3.x) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.y) &&
               yyjson_mut_arr_add_real(doc, arr, value->as_vec3.z) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    case SLAYER3D_VALUE_COLOR: {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        return arr != NULL && yyjson_mut_arr_add_int(doc, arr, value->as_color.r) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.g) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.b) &&
               yyjson_mut_arr_add_int(doc, arr, value->as_color.a) && yyjson_mut_obj_add_val(doc, object, key, arr);
    }
    }
    return false;
}

static bool execute_persistence_save(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (root == NULL)
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    const char *schema = json_string(entry, "schema", NULL);
    if (schema != NULL && !yyjson_mut_obj_add_strcpy(doc, root, "schema", schema))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }
    yyjson_val *version = obj_get(entry, "version");
    if (yyjson_is_int(version) && !yyjson_mut_obj_add_int(doc, root, "version", (int)yyjson_get_int(version)))
    {
        yyjson_mut_doc_free(doc);
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
    {
        const char *key = persistence_property_key(yyjson_arr_get(properties, i));
        if (key == NULL || key[0] == '\0')
            continue;
        const slayer3d_value *value = slayer3d_properties_get_value(actor->props, key);
        if (value != NULL && !json_add_property_value(doc, root, key, value))
        {
            yyjson_mut_doc_free(doc);
            return false;
        }
    }

    size_t size = 0u;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &size);
    yyjson_mut_doc_free(doc);
    if (json == NULL)
        return false;

    char storage_error[256];
    const bool ok =
        ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)) &&
        slayer3d_storage_write_file(runtime->storage, path, json, size, storage_error, (int)sizeof(storage_error));
    free(json);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence save failed: %s", storage_error);
    return ok;
}

static bool execute_persistence_load(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    yyjson_val *entry = find_persistence_entry(runtime, action_persistence_entry_name(action));
    const char *path = json_string(entry, "path", NULL);
    const char *target = json_string(entry, "target", NULL);
    yyjson_val *properties = obj_get(entry, "properties");
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (entry == NULL || path == NULL || actor == NULL || !yyjson_is_arr(properties))
        return false;
    if (!persistence_entry_is_enabled(runtime, entry))
        return true;

    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence storage unavailable: %s", storage_error);
        return false;
    }

    slayer3d_storage_buffer buffer;
    SDL_zero(buffer);
    if (!slayer3d_storage_read_file(runtime->storage, path, &buffer, storage_error, (int)sizeof(storage_error)))
        return true;

    yyjson_doc *doc = yyjson_read_opts((char *)buffer.data, buffer.size, YYJSON_READ_NOFLAG, NULL, NULL);
    yyjson_val *root = doc != NULL ? yyjson_doc_get_root(doc) : NULL;
    const char *schema = json_string(entry, "schema", NULL);
    yyjson_val *version = obj_get(entry, "version");
    const bool schema_ok = schema == NULL || SDL_strcmp(json_string(root, "schema", ""), schema) == 0;
    const bool version_ok =
        !yyjson_is_int(version) || (yyjson_is_int(obj_get(root, "version")) &&
                                    yyjson_get_int(obj_get(root, "version")) == yyjson_get_int(version));
    if (yyjson_is_obj(root) && schema_ok && version_ok)
    {
        for (size_t i = 0; i < yyjson_arr_size(properties); ++i)
        {
            const char *key = persistence_property_key(yyjson_arr_get(properties, i));
            yyjson_val *value = key != NULL ? obj_get(root, key) : NULL;
            if (value != NULL)
                set_actor_property_from_json(actor, key, value);
        }
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D persistence ignored incompatible file: %s", path);
    }

    yyjson_doc_free(doc);
    slayer3d_storage_buffer_free(&buffer);
    return true;
}

static bool execute_persistence_action(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *type)
{
    if (SDL_strcmp(type, "persistence.save") == 0)
        return execute_persistence_save(runtime, action);
    if (SDL_strcmp(type, "persistence.load") == 0)
        return execute_persistence_load(runtime, action);
    return false;
}

static bool ensure_audio_cache_storage(slayer3d_game_data_runtime *runtime)
{
    char storage_error[256];
    if (!ensure_runtime_storage(runtime, storage_error, (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache storage unavailable: %s", storage_error);
        return false;
    }

    if (!slayer3d_storage_create_directory(runtime->storage, "cache://audio", storage_error,
                                           (int)sizeof(storage_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache directory unavailable: %s", storage_error);
        return false;
    }

    char path[4096];
    if (slayer3d_storage_resolve_path(runtime->storage, "cache://audio", path, sizeof(path)))
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio cache directory: %s", path);
    return true;
}

static char *make_audio_cache_path(const slayer3d_game_data_runtime *runtime, const char *filename)
{
    char virtual_path[512];
    char resolved[4096];
    if (runtime == NULL || filename == NULL ||
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) >= (int)sizeof(virtual_path) ||
        !slayer3d_storage_resolve_path(runtime->storage, virtual_path, resolved, sizeof(resolved)))
    {
        return NULL;
    }
    return SDL_strdup(resolved);
}

static char *resolve_authored_audio_path(const slayer3d_game_data_runtime *runtime, const char *path)
{
    if (path == NULL || path[0] == '\0')
        return NULL;
    if (SDL_strncmp(path, "asset://", 8) == 0)
        return SDL_strdup(path);
    return path_join(runtime != NULL ? runtime->base_dir : NULL, path);
}

static char *materialize_audio_asset(slayer3d_game_data_runtime *runtime, const char *path)
{
    char *resolved = resolve_authored_audio_path(runtime, path);
    if (runtime == NULL || resolved == NULL)
        return resolved;

    for (int i = 0; i < runtime->audio_file_count; ++i)
    {
        if (SDL_strcmp(runtime->audio_files[i].asset_path, resolved) == 0)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset cache hit: %s -> %s", resolved,
                        runtime->audio_files[i].file_path);
            SDL_free(resolved);
            return SDL_strdup(runtime->audio_files[i].file_path);
        }
    }

    if (runtime->assets == NULL || !slayer3d_asset_resolver_exists(runtime->assets, resolved))
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio using filesystem path: %s", resolved);
        return resolved;
    }

    if (!ensure_audio_cache_storage(runtime))
    {
        SDL_free(resolved);
        return NULL;
    }

    slayer3d_asset_buffer buffer;
    SDL_zero(buffer);
    char asset_error[256];
    if (!slayer3d_asset_resolver_read_file(runtime->assets, resolved, &buffer, asset_error, (int)sizeof(asset_error)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset read failed: %s", asset_error);
        SDL_free(resolved);
        return NULL;
    }

    char *filename = make_safe_audio_filename(resolved);
    char virtual_path[512];
    const bool has_virtual_path =
        filename != NULL &&
        SDL_snprintf(virtual_path, sizeof(virtual_path), "cache://audio/%s", filename) < (int)sizeof(virtual_path);
    char *file_path = has_virtual_path ? make_audio_cache_path(runtime, filename) : NULL;
    SDL_free(filename);
    if (file_path == NULL)
    {
        slayer3d_asset_buffer_free(&buffer);
        SDL_free(resolved);
        return NULL;
    }

    char storage_error[256];
    const bool wrote = slayer3d_storage_write_file(runtime->storage, virtual_path, buffer.data, buffer.size,
                                                   storage_error, (int)sizeof(storage_error));
    slayer3d_asset_buffer_free(&buffer);
    if (!wrote)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset materialization failed: %s: %s", file_path,
                    storage_error);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }

    if (!ensure_audio_file_capacity(runtime, runtime->audio_file_count + 1))
    {
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    runtime->audio_files[runtime->audio_file_count].asset_path = SDL_strdup(resolved);
    runtime->audio_files[runtime->audio_file_count].file_path = SDL_strdup(file_path);
    if (runtime->audio_files[runtime->audio_file_count].asset_path == NULL ||
        runtime->audio_files[runtime->audio_file_count].file_path == NULL)
    {
        SDL_free(runtime->audio_files[runtime->audio_file_count].asset_path);
        SDL_free(runtime->audio_files[runtime->audio_file_count].file_path);
        SDL_free(file_path);
        SDL_free(resolved);
        return NULL;
    }
    ++runtime->audio_file_count;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio asset materialized: %s -> %s", resolved, file_path);
    SDL_free(resolved);
    return file_path;
}

bool slayer3d_game_data_prepare_audio_file(slayer3d_game_data_runtime *runtime, const char *path, char *out_path,
                                           int out_path_size)
{
    if (out_path != NULL && out_path_size > 0)
        out_path[0] = '\0';
    if (runtime == NULL || path == NULL || path[0] == '\0' || out_path == NULL || out_path_size <= 0)
        return false;

    char *file_path = materialize_audio_asset(runtime, path);
    if (file_path == NULL)
        return false;

    const size_t len = SDL_strlen(file_path);
    const bool ok = len < (size_t)out_path_size;
    if (ok)
        SDL_memcpy(out_path, file_path, len + 1u);
    SDL_free(file_path);
    return ok;
}

static bool execute_audio_play_sfx(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.play_sfx ignored because audio engine is unavailable");
        return true;
    }

    slayer3d_game_data_sound_asset sound;
    const char *sound_id = json_string(action, "sound", json_string(action, "asset", NULL));
    const bool has_asset = slayer3d_game_data_get_sound_asset(runtime, sound_id, &sound);
    const char *path = json_string(action, "path", has_asset ? sound.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx missing sound asset/path: %s",
                    sound_id != NULL ? sound_id : "<none>");
        return false;
    }

    slayer3d_audio_play_desc desc = slayer3d_audio_play_desc_default();
    if (has_asset)
    {
        desc.volume = sound.volume;
        desc.pitch = sound.pitch;
        desc.pan = sound.pan;
        desc.bus = sound.bus;
    }
    desc.volume = json_float(action, "volume", desc.volume);
    desc.pitch = json_float(action, "pitch", desc.pitch);
    desc.pan = json_float(action, "pan", desc.pan);
    desc.bus = parse_audio_bus(json_string(action, "bus", NULL), desc.bus);

    char file_path[4096];
    if (!slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx sound=%s path=%s resolved=%s volume=%.3f bus=%d",
                sound_id != NULL ? sound_id : "<path>", path, file_path, desc.volume, (int)desc.bus);
    const bool ok = slayer3d_audio_play_sound_file(audio, file_path, &desc);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_sfx failed: %s", SDL_GetError());
    return ok;
}

static bool execute_audio_play_music(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.play_music ignored because audio engine is unavailable");
        return true;
    }

    slayer3d_game_data_music_asset music;
    const char *music_id = json_string(action, "music", json_string(action, "asset", NULL));
    const bool has_asset = slayer3d_game_data_get_music_asset(runtime, music_id, &music);
    const char *path = json_string(action, "path", has_asset ? music.path : NULL);
    if (path == NULL)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music missing music asset/path: %s",
                    music_id != NULL ? music_id : "<none>");
        return false;
    }

    const float volume = json_float(action, "volume", has_asset ? music.volume : 1.0f);
    const bool loop = json_bool(action, "loop", has_asset ? music.loop : true);
    const float fade = json_float(action, "fade", json_float(action, "fade_seconds", 0.0f));

    char file_path[4096];
    if (!slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music failed to resolve: %s", path);
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SLAYER3D audio.play_music music=%s path=%s resolved=%s volume=%.3f loop=%d",
                music_id != NULL ? music_id : "<path>", path, file_path, volume, loop ? 1 : 0);
    const bool ok = slayer3d_audio_play_music(audio, file_path, loop, volume, fade);
    if (!ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.play_music failed: %s", SDL_GetError());
    return ok;
}

static bool execute_audio_set_ambient(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                      const slayer3d_properties *payload)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "SLAYER3D audio.set_ambient ignored because audio engine is unavailable");
        return true;
    }

    int ambient_id = json_int(action, "ambient_id", 0);
    const char *ambient_id_from_payload = json_string(action, "ambient_id_from_payload", NULL);
    if (ambient_id_from_payload != NULL)
    {
        const slayer3d_value *value =
            payload != NULL ? slayer3d_properties_get_value(payload, ambient_id_from_payload) : NULL;
        if (value == NULL || value->type != SLAYER3D_VALUE_INT || value->as_int < 0)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SLAYER3D audio.set_ambient missing non-negative int payload value: %s",
                        ambient_id_from_payload);
            return false;
        }
        ambient_id = value->as_int;
    }

    yyjson_val *ambient_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "ambient");
    const int authored_count = yyjson_is_arr(ambient_assets) ? (int)yyjson_arr_size(ambient_assets) : 0;
    const int zone_count = authored_count + 1;
    slayer3d_audio_ambient *zones = (slayer3d_audio_ambient *)SDL_calloc((size_t)zone_count, sizeof(*zones));
    char **resolved_paths = (char **)SDL_calloc((size_t)zone_count, sizeof(*resolved_paths));
    if (zones == NULL || resolved_paths == NULL)
    {
        SDL_free(zones);
        SDL_free(resolved_paths);
        return false;
    }

    zones[0].ambient_id = 0;
    zones[0].path = NULL;
    zones[0].volume = 0.0f;
    zones[0].loop = false;

    bool ok = true;
    for (int i = 0; i < authored_count; ++i)
    {
        yyjson_val *zone = yyjson_arr_get(ambient_assets, (size_t)i);
        const char *path = json_string(zone, "path", NULL);
        char file_path[4096];
        if (path == NULL || !slayer3d_game_data_prepare_audio_file(runtime, path, file_path, sizeof(file_path)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient failed to resolve: %s",
                        path != NULL ? path : "<none>");
            ok = false;
            break;
        }

        resolved_paths[i + 1] = SDL_strdup(file_path);
        if (resolved_paths[i + 1] == NULL)
        {
            ok = false;
            break;
        }
        zones[i + 1].ambient_id = json_int(zone, "ambient_id", 0);
        zones[i + 1].path = resolved_paths[i + 1];
        zones[i + 1].volume = json_float(zone, "volume", 1.0f);
        zones[i + 1].loop = json_bool(zone, "loop", true);
    }

    if (ok)
    {
        const float fade = json_float(action, "fade", json_float(action, "fade_seconds", 0.0f));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient ambient_id=%d fade=%.3f", ambient_id,
                    fade);
        ok = slayer3d_audio_set_ambient(audio, zones, zone_count, ambient_id, fade);
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D audio.set_ambient failed: %s", SDL_GetError());
    }

    for (int i = 0; i < zone_count; ++i)
        SDL_free(resolved_paths[i]);
    SDL_free(resolved_paths);
    SDL_free(zones);
    return ok;
}

static float action_float_or_property(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *key,
                                      float fallback)
{
    yyjson_val *source = obj_get(action, "source");
    if (yyjson_is_obj(source))
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(source, "target", NULL));
        const char *property = json_string(source, "key", NULL);
        const slayer3d_value *value =
            actor != NULL && property != NULL ? slayer3d_properties_get_value(actor->props, property) : NULL;
        if (value != NULL && value->type == SLAYER3D_VALUE_FLOAT)
            return value->as_float * json_float(source, "scale", 1.0f);
        if (value != NULL && value->type == SLAYER3D_VALUE_INT)
            return (float)value->as_int * json_float(source, "scale", 1.0f);
    }
    return json_float(action, key, fallback);
}

static bool execute_audio_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                 const slayer3d_properties *payload, const char *type)
{
    slayer3d_audio_engine *audio = slayer3d_game_session_get_audio(runtime != NULL ? runtime->session : NULL);
    if (SDL_strcmp(type, "audio.play_sfx") == 0)
        return execute_audio_play_sfx(runtime, action);
    if (SDL_strcmp(type, "audio.play_music") == 0)
        return execute_audio_play_music(runtime, action);
    if (SDL_strcmp(type, "audio.set_ambient") == 0)
        return execute_audio_set_ambient(runtime, action, payload);
    if (audio == NULL)
    {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D %s ignored because audio engine is unavailable", type);
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_sfx") == 0)
    {
        slayer3d_audio_stop_bus(audio,
                                parse_audio_bus(json_string(action, "bus", NULL), SLAYER3D_AUDIO_BUS_SOUND_EFFECTS));
        return true;
    }
    if (SDL_strcmp(type, "audio.stop_music") == 0)
    {
        slayer3d_audio_stop_music(audio, json_float(action, "fade", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.fade_music") == 0)
    {
        slayer3d_audio_fade_music(audio, json_float(action, "volume", 1.0f),
                                  json_float(action, "duration", json_float(action, "fade_seconds", 0.0f)));
        return true;
    }
    if (SDL_strcmp(type, "audio.set_bus_volume") == 0)
    {
        slayer3d_audio_set_bus_volume(audio,
                                      parse_audio_bus(json_string(action, "bus", NULL), SLAYER3D_AUDIO_BUS_COUNT),
                                      action_float_or_property(runtime, action, "volume", 1.0f));
        return true;
    }
    return false;
}

static bool json_string_array_contains(yyjson_val *array, const char *value)
{
    if (!yyjson_is_arr(array) || value == NULL)
        return false;
    for (size_t i = 0; i < yyjson_arr_size(array); ++i)
    {
        const char *entry = yyjson_get_str(yyjson_arr_get(array, i));
        if (entry != NULL && SDL_strcmp(entry, value) == 0)
            return true;
    }
    return false;
}

static property_snapshot *find_property_snapshot(slayer3d_game_data_runtime *runtime, const char *name,
                                                 const char *target)
{
    if (runtime == NULL || name == NULL || target == NULL)
        return NULL;
    for (int i = 0; i < runtime->property_snapshot_count; ++i)
    {
        property_snapshot *snapshot = &runtime->property_snapshots[i];
        if (snapshot->name != NULL && snapshot->target != NULL && SDL_strcmp(snapshot->name, name) == 0 &&
            SDL_strcmp(snapshot->target, target) == 0)
            return snapshot;
    }
    return NULL;
}

static property_snapshot *get_or_create_property_snapshot(slayer3d_game_data_runtime *runtime, const char *name,
                                                          const char *target)
{
    property_snapshot *existing = find_property_snapshot(runtime, name, target);
    if (existing != NULL)
        return existing;
    if (runtime == NULL || name == NULL || name[0] == '\0' || target == NULL || target[0] == '\0')
        return NULL;
    if (runtime->property_snapshot_count >= runtime->property_snapshot_capacity)
    {
        const int next_capacity = runtime->property_snapshot_capacity > 0 ? runtime->property_snapshot_capacity * 2 : 4;
        property_snapshot *next = (property_snapshot *)SDL_realloc(
            runtime->property_snapshots, (size_t)next_capacity * sizeof(*runtime->property_snapshots));
        if (next == NULL)
            return NULL;
        SDL_memset(next + runtime->property_snapshot_capacity, 0,
                   (size_t)(next_capacity - runtime->property_snapshot_capacity) *
                       sizeof(*runtime->property_snapshots));
        runtime->property_snapshots = next;
        runtime->property_snapshot_capacity = next_capacity;
    }

    property_snapshot *snapshot = &runtime->property_snapshots[runtime->property_snapshot_count];
    snapshot->name = SDL_strdup(name);
    snapshot->target = SDL_strdup(target);
    snapshot->properties = slayer3d_properties_create();
    if (snapshot->name == NULL || snapshot->target == NULL || snapshot->properties == NULL)
    {
        SDL_free(snapshot->name);
        SDL_free(snapshot->target);
        slayer3d_properties_destroy(snapshot->properties);
        SDL_zero(*snapshot);
        return NULL;
    }
    runtime->property_snapshot_count++;
    return snapshot;
}

static void copy_selected_properties(slayer3d_properties *target, const slayer3d_properties *source, yyjson_val *keys)
{
    if (target == NULL || source == NULL)
        return;
    if (yyjson_is_arr(keys))
    {
        for (size_t i = 0; i < yyjson_arr_size(keys); ++i)
        {
            const char *key = yyjson_get_str(yyjson_arr_get(keys, i));
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
        }
        return;
    }

    const int count = slayer3d_properties_count(source);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(source, i, &key, NULL))
            copy_property_value(target, key, slayer3d_properties_get_value(source, key));
    }
}

static bool snapshot_actor_properties(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = get_or_create_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    slayer3d_properties_clear(snapshot->properties);
    copy_selected_properties(snapshot->properties, actor->props, obj_get(action, "keys"));
    return true;
}

static bool restore_actor_property_snapshot(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    const char *name = json_string(action, "name", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    property_snapshot *snapshot = find_property_snapshot(runtime, name, target);
    if (actor == NULL || snapshot == NULL)
        return false;

    copy_selected_properties(actor->props, snapshot->properties, obj_get(action, "keys"));
    return true;
}

static bool reset_actor_properties_to_authored_defaults(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    yyjson_val *entity = find_entity_json(runtime, target);
    yyjson_val *properties = obj_get(entity, "properties");
    yyjson_val *keys = obj_get(action, "keys");
    if (actor == NULL || !yyjson_is_obj(properties))
        return false;

    yyjson_val *key;
    yyjson_val *entry;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        if (yyjson_is_arr(keys) && !json_string_array_contains(keys, name))
            continue;
        entry = yyjson_obj_iter_get_val(key);
        yyjson_val *value = obj_get(entry, "value");
        if (value != NULL)
            set_actor_property_from_json(actor, name, value);
    }
    return true;
}

static actor_pool_runtime *find_actor_pool(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        if (runtime->actor_pools[i].name != NULL && SDL_strcmp(runtime->actor_pools[i].name, name) == 0)
            return &runtime->actor_pools[i];
    }
    return NULL;
}

static const actor_pool_runtime *find_actor_pool_const(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        if (runtime->actor_pools[i].name != NULL && SDL_strcmp(runtime->actor_pools[i].name, name) == 0)
            return &runtime->actor_pools[i];
    }
    return NULL;
}

static int actor_pool_index_for_actor(const actor_pool_runtime *pool, const char *actor_name)
{
    if (pool == NULL || actor_name == NULL)
        return -1;
    for (int i = 0; i < pool->capacity; ++i)
    {
        if (pool->actor_names[i] != NULL && SDL_strcmp(pool->actor_names[i], actor_name) == 0)
            return i;
    }
    return -1;
}

static actor_pool_runtime *find_actor_pool_for_actor(slayer3d_game_data_runtime *runtime, const char *actor_name,
                                                     int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

static const actor_pool_runtime *find_actor_pool_for_actor_const(const slayer3d_game_data_runtime *runtime,
                                                                 const char *actor_name, int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || actor_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->actor_pool_count; ++i)
    {
        const int index = actor_pool_index_for_actor(&runtime->actor_pools[i], actor_name);
        if (index >= 0)
        {
            if (out_index != NULL)
                *out_index = index;
            return &runtime->actor_pools[i];
        }
    }
    return NULL;
}

static slayer3d_registered_actor *actor_pool_allocate(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                                      int *out_index)
{
    if (out_index != NULL)
        *out_index = -1;
    if (runtime == NULL || pool == NULL)
        return NULL;

    for (int i = 0; i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (actor_pool_actor_is_available(pool, actor, i))
        {
            if (out_index != NULL)
                *out_index = i;
            return actor;
        }
    }

    if (pool->exhaustion != ACTOR_POOL_EXHAUST_REUSE_OLDEST || pool->capacity <= 0)
    {
        if (pool != NULL)
        {
            pool->exhaustion_count++;
            actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason),
                                   "exhausted");
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D actor pool exhausted: pool=%s capacity=%d policy=fail",
                        pool->name != NULL ? pool->name : "<unnamed>", pool->capacity);
        }
        return NULL;
    }

    int index = -1;
    Uint64 oldest_generation = SDL_MAX_UINT64;
    for (int i = 0; i < pool->capacity; ++i)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[i]);
        if (!actor_pool_actor_is_active(pool, actor, i))
            continue;
        const Uint64 generation = pool->spawn_generations != NULL ? pool->spawn_generations[i] : 0U;
        if (index < 0 || generation < oldest_generation)
        {
            index = i;
            oldest_generation = generation;
        }
    }
    if (index < 0)
    {
        pool->exhaustion_count++;
        actor_pool_copy_reason(pool->last_spawn_failure_reason, sizeof(pool->last_spawn_failure_reason), "exhausted");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SLAYER3D actor pool exhausted: pool=%s capacity=%d no reusable actor",
                    pool->name != NULL ? pool->name : "<unnamed>", pool->capacity);
        return NULL;
    }
    if (out_index != NULL)
        *out_index = index;
    pool->exhaustion_count++;
    pool->reuse_count++;
    pool->despawn_count++;
    actor_pool_copy_reason(pool->last_despawn_reason, sizeof(pool->last_despawn_reason), "reuse_oldest");
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D actor pool exhausted: pool=%s capacity=%d reusing actor=%s",
                pool->name != NULL ? pool->name : "<unnamed>", pool->capacity,
                pool->actor_names[index] != NULL ? pool->actor_names[index] : "<unnamed>");
    return slayer3d_game_data_find_actor(runtime, pool->actor_names[index]);
}

static bool actor_pool_request_despawn(slayer3d_game_data_runtime *runtime, actor_pool_runtime *pool,
                                       slayer3d_registered_actor *actor, int index, const char *reason)
{
    if (runtime == NULL || pool == NULL || actor == NULL || index < 0 || index >= pool->capacity)
        return false;

    if (!actor->active && actor_pool_lifecycle_state(pool, index) == ACTOR_LIFECYCLE_INACTIVE)
        return true;

    pool->despawn_count++;
    actor_pool_copy_reason(pool->last_despawn_reason, sizeof(pool->last_despawn_reason), reason);
    actor->active = false;
    actor_pool_set_lifecycle_state(pool, actor, index, ACTOR_LIFECYCLE_DESPAWNING);
    if (runtime->actor_lifecycle_defer_depth > 0)
    {
        runtime->actor_lifecycle_flush_pending = true;
        return true;
    }
    if (!initialize_pooled_actor(pool, actor, index, false))
        return false;
    slayer3d_properties_set_string(actor->props, "pool_last_despawn_reason", pool->last_despawn_reason);
    return true;
}

static void apply_actor_spawn_properties(slayer3d_registered_actor *actor, yyjson_val *properties)
{
    if (actor == NULL || !yyjson_is_obj(properties))
        return;

    yyjson_val *key;
    yyjson_val *value;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(properties, &iter);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL)
    {
        const char *name = yyjson_get_str(key);
        value = yyjson_obj_iter_get_val(key);
        if (name != NULL)
            set_actor_property_from_json(actor, name, value);
    }
}

static slayer3d_registered_actor *actor_from_payload_key(slayer3d_game_data_runtime *runtime,
                                                         const slayer3d_properties *payload, const char *key)
{
    const char *actor_name = payload != NULL && key != NULL ? slayer3d_properties_get_string(payload, key, NULL) : NULL;
    return slayer3d_game_data_find_actor(runtime, actor_name);
}

static slayer3d_vec3 actor_spawn_position_from_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload, slayer3d_vec3 fallback)
{
    slayer3d_vec3 position = fallback;
    yyjson_val *position_json = obj_get(action, "position");
    if (position_json != NULL)
        position = json_vec3_value(position_json, position);

    const char *from_payload_key = json_string(action, "from_payload", NULL);
    slayer3d_registered_actor *from_payload_actor = actor_from_payload_key(runtime, payload, from_payload_key);
    if (from_payload_actor != NULL)
        position = from_payload_actor->position;

    const char *from_actor_name = json_string(action, "from", NULL);
    slayer3d_registered_actor *from_actor = slayer3d_game_data_find_actor(runtime, from_actor_name);
    if (from_actor != NULL)
        position = from_actor->position;

    yyjson_val *offset_json = obj_get(action, "offset");
    if (offset_json != NULL)
    {
        const slayer3d_vec3 offset = json_vec3_value(offset_json, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
        position.x += offset.x;
        position.y += offset.y;
        position.z += offset.z;
    }
    return position;
}

static bool execute_actor_spawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                       const slayer3d_properties *payload)
{
    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(action, "pool", NULL));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        return false;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        return false;
    }
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }
    actor_set_position(actor, actor_spawn_position_from_action(runtime, action, payload, actor->position));
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    actor_pool_note_spawn_success(runtime, pool);

    const char *actor_key = json_string(action, "output_actor_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && actor_key != NULL)
        slayer3d_properties_set_string(runtime->scene_state, actor_key, actor->name);
    const char *id_key = json_string(action, "output_id_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && id_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, id_key, actor->id);
    const char *pool_index_key = json_string(action, "output_pool_index_key", NULL);
    if (runtime != NULL && runtime->scene_state != NULL && pool_index_key != NULL)
        slayer3d_properties_set_int(runtime->scene_state, pool_index_key, actor_index);
    return true;
}

static bool execute_actor_despawn_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *target = json_string(action, "target", NULL);
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, target);
    if (actor == NULL)
        return false;

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    if (pool != NULL && actor_index >= 0)
        return actor_pool_request_despawn(runtime, pool, actor, actor_index, json_string(action, "reason", "action"));

    actor->active = false;
    return true;
}

static bool execute_actor_despawn_action_with_payload(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor =
        actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (actor == NULL)
        return execute_actor_despawn_action(runtime, action);

    int actor_index = -1;
    actor_pool_runtime *pool = find_actor_pool_for_actor(runtime, actor->name, &actor_index);
    if (pool != NULL && actor_index >= 0)
        return actor_pool_request_despawn(runtime, pool, actor, actor_index, json_string(action, "reason", "action"));

    actor->active = false;
    return true;
}

static bool execute_actor_despawn_by_tag_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const char *tag = json_string(action, "tag", NULL);
    if (runtime == NULL || tag == NULL || tag[0] == '\0')
        return false;

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!entity_json_has_tags(pool->archetype_json, &tag, 1))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index))
            {
                (void)actor_pool_request_despawn(runtime, pool, actor, actor_index,
                                                 json_string(action, "reason", "action_tag"));
            }
        }
    }
    return true;
}

typedef enum weapon_fire_status
{
    WEAPON_FIRE_READY = 0,
    WEAPON_FIRE_COOLDOWN,
    WEAPON_FIRE_RELOADING,
    WEAPON_FIRE_EMPTY
} weapon_fire_status;

static bool weapon_value_as_float(const slayer3d_value *value, float *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    return false;
}

static float weapon_actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback)
{
    float value = fallback;
    return actor != NULL && actor->props != NULL &&
                   weapon_value_as_float(slayer3d_properties_get_value(actor->props, key), &value)
               ? value
               : fallback;
}

static void weapon_set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value)
{
    if (actor == NULL || actor->props == NULL || key == NULL || key[0] == '\0')
        return;
    const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
    const float rounded = SDL_roundf(value);
    if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && SDL_fabsf(value - rounded) <= 0.0001f)
        slayer3d_properties_set_int(actor->props, key, (int)rounded);
    else
        slayer3d_properties_set_float(actor->props, key, value);
}

static slayer3d_properties *weapon_event_payload(const slayer3d_registered_actor *source, yyjson_val *action,
                                                 weapon_fire_status status)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    const char *status_text = "ready";
    if (status == WEAPON_FIRE_COOLDOWN)
        status_text = "cooldown";
    else if (status == WEAPON_FIRE_RELOADING)
        status_text = "reloading";
    else if (status == WEAPON_FIRE_EMPTY)
        status_text = "empty";
    slayer3d_properties_set_string(payload, "weapon_status", status_text);
    slayer3d_properties_set_float(payload, "ammo_per_shot", json_float(action, "ammo_per_shot", 1.0f));
    return payload;
}

static void weapon_emit_signal(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *signal_key,
                               const slayer3d_registered_actor *source, weapon_fire_status status)
{
    const int signal_id = action_signal_id(runtime, action, signal_key);
    if (signal_id < 0 || runtime_bus(runtime) == NULL)
        return;
    slayer3d_properties *payload = weapon_event_payload(source, action, status);
    slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
    slayer3d_properties_destroy(payload);
}

static weapon_fire_status weapon_fire_status_for_actor(const slayer3d_registered_actor *actor, yyjson_val *action)
{
    if (actor == NULL || action == NULL)
        return WEAPON_FIRE_EMPTY;

    const char *cooldown_property = json_string(action, "cooldown_property", "fire_timer");
    if (cooldown_property != NULL && cooldown_property[0] != '\0' &&
        slayer3d_properties_get_float(actor->props, cooldown_property, 0.0f) > 0.0f)
    {
        return WEAPON_FIRE_COOLDOWN;
    }

    const char *reload_timer_property = json_string(action, "reload_timer_property", NULL);
    if (reload_timer_property != NULL && reload_timer_property[0] != '\0' &&
        slayer3d_properties_get_float(actor->props, reload_timer_property, 0.0f) > 0.0f)
    {
        return WEAPON_FIRE_RELOADING;
    }

    const float ammo_per_shot = SDL_max(json_float(action, "ammo_per_shot", 1.0f), 0.0f);
    const char *clip_property = json_string(action, "clip_property", NULL);
    if (clip_property != NULL && clip_property[0] != '\0' &&
        weapon_actor_numeric_property(actor, clip_property, 0.0f) + 0.0001f < ammo_per_shot)
    {
        return WEAPON_FIRE_EMPTY;
    }

    const char *ammo_resource = json_string(action, "ammo_resource", NULL);
    const char *ammo_property = json_string(action, "ammo_property", ammo_resource);
    if (clip_property == NULL && ammo_property != NULL && ammo_property[0] != '\0' &&
        weapon_actor_numeric_property(actor, ammo_property, 0.0f) + 0.0001f < ammo_per_shot)
    {
        return WEAPON_FIRE_EMPTY;
    }
    return WEAPON_FIRE_READY;
}

static bool weapon_fire_prepare(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                const slayer3d_registered_actor *actor)
{
    const weapon_fire_status status = weapon_fire_status_for_actor(actor, action);
    if (status == WEAPON_FIRE_READY)
        return true;
    if (status == WEAPON_FIRE_COOLDOWN)
        weapon_emit_signal(runtime, action, "on_cooldown", actor, status);
    else if (status == WEAPON_FIRE_RELOADING)
        weapon_emit_signal(runtime, action, "on_reloading", actor, status);
    else
        weapon_emit_signal(runtime, action, "on_empty", actor, status);
    return false;
}

static void weapon_fire_commit(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               slayer3d_registered_actor *actor)
{
    if (actor == NULL || action == NULL)
        return;

    const float ammo_per_shot = SDL_max(json_float(action, "ammo_per_shot", 1.0f), 0.0f);
    const char *clip_property = json_string(action, "clip_property", NULL);
    if (clip_property != NULL && clip_property[0] != '\0')
    {
        const float clip = weapon_actor_numeric_property(actor, clip_property, 0.0f);
        weapon_set_actor_numeric_property(actor, clip_property, SDL_max(clip - ammo_per_shot, 0.0f));
    }
    else
    {
        const char *ammo_resource = json_string(action, "ammo_resource", NULL);
        const char *ammo_property = json_string(action, "ammo_property", ammo_resource);
        if (ammo_property != NULL && ammo_property[0] != '\0')
        {
            const float ammo = weapon_actor_numeric_property(actor, ammo_property, 0.0f);
            weapon_set_actor_numeric_property(actor, ammo_property, SDL_max(ammo - ammo_per_shot, 0.0f));
        }
    }

    const char *cooldown_property = json_string(action, "cooldown_property", "fire_timer");
    if (cooldown_property != NULL && cooldown_property[0] != '\0')
    {
        const float cooldown =
            json_float(action, "cooldown", slayer3d_properties_get_float(actor->props, "fire_cooldown", 0.0f));
        slayer3d_properties_set_float(actor->props, cooldown_property, cooldown);
    }
    weapon_emit_signal(runtime, action, "on_fire", actor, WEAPON_FIRE_READY);
}

static bool execute_projectile_fire_action_for_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                     const slayer3d_properties *payload,
                                                     slayer3d_registered_actor *source_actor)
{
    const char *target_name = json_string(action, "target", NULL);
    slayer3d_registered_actor *target =
        source_actor != NULL ? source_actor : slayer3d_game_data_find_actor(runtime, target_name);
    if (target == NULL)
        target = actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (target == NULL || !runtime_actor_is_active(runtime, target))
        return false;

    if (!weapon_fire_prepare(runtime, action, target))
        return true;

    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(action, "pool", NULL));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        return false;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        return false;
    }
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    actor_set_position(actor, actor_spawn_position_from_action(runtime, action, payload, target->position));
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    yyjson_val *velocity = obj_get(action, "velocity");
    const slayer3d_vec3 fallback_velocity = json_vec3_value(velocity, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    const char *velocity_property = json_string(action, "velocity_from_property", NULL);
    if (velocity_property != NULL || velocity != NULL)
    {
        slayer3d_vec3 projectile_velocity =
            velocity_property != NULL
                ? slayer3d_properties_get_vec3(target->props, velocity_property, fallback_velocity)
                : fallback_velocity;
        yyjson_val *speed_value = obj_get(action, "speed");
        if (yyjson_is_num(speed_value))
        {
            const float speed = (float)yyjson_get_num(speed_value);
            if (slayer3d_vec3_length_squared(projectile_velocity) > 0.000001f)
                projectile_velocity = slayer3d_vec3_scale(slayer3d_vec3_normalize(projectile_velocity), speed);
            else
                projectile_velocity = slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
        }
        slayer3d_properties_set_vec3(actor->props, "velocity", projectile_velocity);
    }
    actor_pool_note_spawn_success(runtime, pool);
    weapon_fire_commit(runtime, action, target);
    return true;
}

static bool value_as_float(const slayer3d_value *value, float *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (value->type == SLAYER3D_VALUE_INT)
    {
        *out_value = (float)value->as_int;
        return true;
    }
    if (value->type == SLAYER3D_VALUE_FLOAT)
    {
        *out_value = value->as_float;
        return true;
    }
    return false;
}

static bool action_float_value(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload, const char *key, const char *payload_key,
                               float *out_value)
{
    (void)runtime;
    if (action == NULL || out_value == NULL)
        return false;

    yyjson_val *value = obj_get(action, key);
    if (yyjson_is_num(value))
    {
        *out_value = (float)yyjson_get_num(value);
        return true;
    }

    const char *payload_field = json_string(action, payload_key, NULL);
    if (payload_field != NULL && payload != NULL)
        return value_as_float(slayer3d_properties_get_value(payload, payload_field), out_value);
    return false;
}

static void set_actor_numeric_property(slayer3d_registered_actor *actor, const char *key, float value)
{
    if (actor == NULL || actor->props == NULL || key == NULL)
        return;

    const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
    const float rounded = SDL_roundf(value);
    if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && SDL_fabsf(value - rounded) <= 0.0001f)
        slayer3d_properties_set_int(actor->props, key, (int)rounded);
    else
        slayer3d_properties_set_float(actor->props, key, value);
}

static float actor_numeric_property(const slayer3d_registered_actor *actor, const char *key, float fallback)
{
    float value = fallback;
    return actor != NULL && actor->props != NULL &&
                   value_as_float(slayer3d_properties_get_value(actor->props, key), &value)
               ? value
               : fallback;
}

static const char *combat_action_property(yyjson_val *action, const char *key, const char *fallback)
{
    const char *value = json_string(action, key, NULL);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static slayer3d_properties *combat_event_payload(slayer3d_registered_actor *target, yyjson_val *action,
                                                 const slayer3d_properties *source_payload, float amount,
                                                 float armor_delta, float health_delta, float health, float max_health,
                                                 float armor, bool alive)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;

    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    const char *source = json_string(action, "source", NULL);
    const char *source_from_payload = json_string(action, "source_from_payload", NULL);
    if (source_from_payload != NULL && source_payload != NULL)
        source = slayer3d_properties_get_string(source_payload, source_from_payload, source);
    slayer3d_properties_set_string(event_payload, "source_actor_name", source != NULL ? source : "");
    slayer3d_properties_set_string(event_payload, "damage_type", json_string(action, "damage_type", ""));
    slayer3d_properties_set_float(event_payload, "amount", amount);
    slayer3d_properties_set_float(event_payload, "armor_delta", armor_delta);
    slayer3d_properties_set_float(event_payload, "health_delta", health_delta);
    slayer3d_properties_set_float(event_payload, "health", health);
    slayer3d_properties_set_float(event_payload, "max_health", max_health);
    slayer3d_properties_set_float(event_payload, "armor", armor);
    slayer3d_properties_set_bool(event_payload, "alive", alive);
    slayer3d_properties_set_bool(event_payload, "dead", !alive);
    return event_payload;
}

static void emit_combat_signal(slayer3d_game_data_runtime *runtime, yyjson_val *action, const char *signal_key,
                               const slayer3d_properties *payload)
{
    const int signal_id = action_signal_id(runtime, action, signal_key);
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
}

static bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload, slayer3d_registered_actor *actor,
                                         float amount);

static bool execute_combat_damage_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (actor == NULL || !action_float_value(runtime, action, payload, "amount", "amount_from_payload", &amount))
        return false;
    amount = SDL_max(amount, 0.0f);

    return apply_combat_damage_to_actor(runtime, action, payload, actor, amount);
}

static bool apply_combat_damage_to_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload, slayer3d_registered_actor *actor,
                                         float amount)
{
    if (actor == NULL)
        return false;
    slayer3d_registered_actor *source = action_source_actor(runtime, action, payload);
    if (!actor_matches_target_filter(runtime, actor, source, action, payload, NULL, false))
        return true;
    amount = SDL_max(amount, 0.0f);

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *armor_absorb_key = combat_action_property(action, "armor_absorb_property", "armor_absorb");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");

    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    const float old_health = SDL_clamp(actor_numeric_property(actor, health_key, max_health), 0.0f, max_health);
    const float old_armor = SDL_max(actor_numeric_property(actor, armor_key, 0.0f), 0.0f);
    const float authored_absorb =
        json_float(action, "armor_absorb", actor_numeric_property(actor, armor_absorb_key, 1.0f));
    const float armor_absorb = old_armor > 0.0f ? SDL_clamp(authored_absorb, 0.0f, 1.0f) : 0.0f;
    const float armor_delta = SDL_min(old_armor, amount * armor_absorb);
    const float health_delta = SDL_max(amount - armor_delta, 0.0f);
    const float health = SDL_max(old_health - health_delta, 0.0f);
    const float armor = SDL_max(old_armor - armor_delta, 0.0f);
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);
    const bool alive = health > 0.0f;

    set_actor_numeric_property(actor, health_key, health);
    set_actor_numeric_property(actor, armor_key, armor);
    slayer3d_properties_set_bool(actor->props, alive_key, alive);
    if (!alive && json_bool(action, "deactivate_on_death", false))
        actor->active = false;

    slayer3d_properties *event_payload = combat_event_payload(actor, action, payload, amount, armor_delta, health_delta,
                                                              health, max_health, armor, alive);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_damage", event_payload);
        if (was_alive && !alive)
            emit_combat_signal(runtime, action, "on_death", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool execute_combat_heal_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                       const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (actor == NULL || !action_float_value(runtime, action, payload, "amount", "amount_from_payload", &amount))
        return false;
    amount = SDL_max(amount, 0.0f);

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    const float old_health = SDL_clamp(actor_numeric_property(actor, health_key, max_health), 0.0f, max_health);
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);
    const bool can_revive = json_bool(action, "revive", false);
    const float health = SDL_min(old_health + amount, max_health);
    const bool alive = (was_alive || can_revive) && health > 0.0f;

    set_actor_numeric_property(actor, health_key, health);
    slayer3d_properties_set_bool(actor->props, alive_key, alive);

    slayer3d_properties *event_payload = combat_event_payload(
        actor, action, payload, amount, 0.0f, -amount, health, max_health,
        actor_numeric_property(actor, combat_action_property(action, "armor_property", "armor"), 0.0f), alive);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_heal", event_payload);
        if (!was_alive && alive)
            emit_combat_signal(runtime, action, "on_revive", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool execute_combat_kill_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                       const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float old_health =
        actor_numeric_property(actor, health_key, actor_numeric_property(actor, max_health_key, 100.0f));
    const bool was_alive = slayer3d_properties_get_bool(actor->props, alive_key, old_health > 0.0f);

    set_actor_numeric_property(actor, health_key, 0.0f);
    slayer3d_properties_set_bool(actor->props, alive_key, false);
    if (json_bool(action, "deactivate", json_bool(action, "deactivate_on_death", false)))
        actor->active = false;

    if (was_alive)
    {
        slayer3d_properties *event_payload =
            combat_event_payload(actor, action, payload, old_health, 0.0f, old_health, 0.0f,
                                 actor_numeric_property(actor, max_health_key, 100.0f),
                                 actor_numeric_property(actor, armor_key, 0.0f), false);
        if (event_payload != NULL)
        {
            emit_combat_signal(runtime, action, "on_death", event_payload);
            slayer3d_properties_destroy(event_payload);
        }
    }
    return true;
}

static bool execute_combat_revive_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *health_key = combat_action_property(action, "health_property", "health");
    const char *max_health_key = combat_action_property(action, "max_health_property", "max_health");
    const char *armor_key = combat_action_property(action, "armor_property", "armor");
    const char *alive_key = combat_action_property(action, "alive_property", "alive");
    const float max_health = SDL_max(actor_numeric_property(actor, max_health_key, 100.0f), 0.0f);
    float health = max_health;
    (void)action_float_value(runtime, action, payload, "health", "health_from_payload", &health);
    health = SDL_clamp(health, 0.0f, max_health);

    set_actor_numeric_property(actor, health_key, health);
    slayer3d_properties_set_bool(actor->props, alive_key, health > 0.0f);
    actor->active = true;

    slayer3d_properties *event_payload =
        combat_event_payload(actor, action, payload, health, 0.0f, -health, health, max_health,
                             actor_numeric_property(actor, armor_key, 0.0f), health > 0.0f);
    if (event_payload != NULL)
    {
        emit_combat_signal(runtime, action, "on_revive", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static const char *resource_name(yyjson_val *action)
{
    const char *resource = json_string(action, "resource", NULL);
    return resource != NULL && resource[0] != '\0' ? resource : NULL;
}

static const char *resource_property_name(yyjson_val *json, const char *fallback_resource)
{
    const char *property = json_string(json, "property", NULL);
    if (property != NULL && property[0] != '\0')
        return property;
    return fallback_resource;
}

static const char *resource_max_property_name(yyjson_val *json, const char *resource, char *buffer, size_t buffer_size)
{
    const char *property = json_string(json, "max_property", NULL);
    if (property != NULL && property[0] != '\0')
        return property;
    if (resource == NULL || resource[0] == '\0' || buffer == NULL || buffer_size == 0U)
        return NULL;
    SDL_snprintf(buffer, buffer_size, "max_%s", resource);
    return buffer;
}

static bool resource_numeric_value(yyjson_val *json, const slayer3d_properties *payload, const char *value_key,
                                   const char *payload_key, float *out_value)
{
    return action_float_value(NULL, json, payload, value_key, payload_key, out_value);
}

static slayer3d_properties *resource_event_payload(slayer3d_registered_actor *target, yyjson_val *json,
                                                   const char *resource, float old_value, float value, float max_value,
                                                   float amount, bool success)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;
    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "resource", resource != NULL ? resource : "");
    slayer3d_properties_set_string(
        event_payload, "resource_property",
        resource_property_name(json, resource) != NULL ? resource_property_name(json, resource) : "");
    slayer3d_properties_set_float(event_payload, "old_value", old_value);
    slayer3d_properties_set_float(event_payload, "value", value);
    slayer3d_properties_set_float(event_payload, "max_value", max_value);
    slayer3d_properties_set_float(event_payload, "amount", amount);
    slayer3d_properties_set_bool(event_payload, "success", success);
    return event_payload;
}

static void emit_optional_signal(slayer3d_game_data_runtime *runtime, yyjson_val *json, const char *signal_key,
                                 const slayer3d_properties *payload)
{
    const int signal_id = action_signal_id(runtime, json, signal_key);
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
}

static bool apply_resource_delta_to_actor(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *target,
                                          yyjson_val *json, const slayer3d_properties *payload, float amount,
                                          bool consume)
{
    const char *resource = resource_name(json);
    const char *property = resource_property_name(json, resource);
    char max_property_buffer[128];
    const char *max_property =
        resource_max_property_name(json, resource, max_property_buffer, sizeof(max_property_buffer));
    if (target == NULL || resource == NULL || property == NULL)
        return false;

    const float old_value = actor_numeric_property(target, property, 0.0f);
    const float min_value = json_float(json, "min", 0.0f);
    float max_value = 0.0f;
    const bool has_authored_max = yyjson_is_num(obj_get(json, "max"));
    const bool has_max_property =
        max_property != NULL && slayer3d_properties_get_value(target->props, max_property) != NULL;
    if (has_authored_max)
        max_value = json_float(json, "max", old_value);
    else if (has_max_property)
        max_value = actor_numeric_property(target, max_property, old_value);

    bool success = true;
    float applied = SDL_max(amount, 0.0f);
    float value = old_value;
    if (consume)
    {
        const bool allow_partial = json_bool(json, "allow_partial", false);
        if (old_value + 0.0001f < applied && !allow_partial)
        {
            success = false;
            applied = 0.0f;
        }
        else
        {
            applied = SDL_min(applied, SDL_max(old_value - min_value, 0.0f));
            value = old_value - applied;
        }
    }
    else
    {
        value = old_value + applied;
    }

    if (success && json_bool(json, "clamp", true))
    {
        value = SDL_max(value, min_value);
        if (has_authored_max || has_max_property)
            value = SDL_min(value, max_value);
    }

    if (success)
        set_actor_numeric_property(target, property, value);

    slayer3d_properties *event_payload =
        resource_event_payload(target, json, resource, old_value, success ? value : old_value,
                               (has_authored_max || has_max_property) ? max_value : old_value, applied, success);
    if (event_payload != NULL)
    {
        emit_optional_signal(runtime, json, success ? "on_success" : "on_failure", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    (void)payload;
    return true;
}

static bool execute_resource_amount_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                           const slayer3d_properties *payload, bool consume)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    float amount = 0.0f;
    if (target == NULL || !resource_numeric_value(action, payload, "amount", "amount_from_payload", &amount))
        return false;
    return apply_resource_delta_to_actor(runtime, target, action, payload, amount, consume);
}

static bool execute_resource_set_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    const char *resource = resource_name(action);
    const char *property = resource_property_name(action, resource);
    float value = 0.0f;
    if (target == NULL || resource == NULL || property == NULL ||
        !resource_numeric_value(action, payload, "value", "value_from_payload", &value))
    {
        return false;
    }

    char max_property_buffer[128];
    const char *max_property =
        resource_max_property_name(action, resource, max_property_buffer, sizeof(max_property_buffer));
    const bool has_authored_max = yyjson_is_num(obj_get(action, "max"));
    const bool has_max_property =
        max_property != NULL && slayer3d_properties_get_value(target->props, max_property) != NULL;
    const float max_value = has_authored_max   ? json_float(action, "max", value)
                            : has_max_property ? actor_numeric_property(target, max_property, value)
                                               : value;
    const float min_value = json_float(action, "min", 0.0f);
    const float old_value = actor_numeric_property(target, property, 0.0f);
    if (json_bool(action, "clamp", true))
    {
        value = SDL_max(value, min_value);
        if (has_authored_max || has_max_property)
            value = SDL_min(value, max_value);
    }
    set_actor_numeric_property(target, property, value);

    slayer3d_properties *event_payload =
        resource_event_payload(target, action, resource, old_value, value,
                               (has_authored_max || has_max_property) ? max_value : value, value - old_value, true);
    if (event_payload != NULL)
    {
        emit_optional_signal(runtime, action, "on_success", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool apply_resource_grants(slayer3d_game_data_runtime *runtime, slayer3d_registered_actor *target,
                                  yyjson_val *grants, const slayer3d_properties *payload)
{
    if (runtime == NULL || target == NULL || !yyjson_is_arr(grants))
        return false;

    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(grants); ++i)
    {
        yyjson_val *grant = yyjson_arr_get(grants, i);
        float amount = 0.0f;
        if (!resource_numeric_value(grant, payload, "amount", "amount_from_payload", &amount) ||
            !apply_resource_delta_to_actor(runtime, target, grant, payload, amount, false))
        {
            ok = false;
        }
    }
    return ok;
}

static bool execute_pickup_collect_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload)
{
    slayer3d_registered_actor *collector = action_target_actor(runtime, action, payload);
    slayer3d_registered_actor *pickup = slayer3d_game_data_find_actor(runtime, json_string(action, "pickup", NULL));
    const char *pickup_from_payload = json_string(action, "pickup_from_payload", NULL);
    if (pickup == NULL && pickup_from_payload != NULL)
        pickup = actor_from_payload_key(runtime, payload, pickup_from_payload);
    if (collector == NULL || pickup == NULL || !pickup->active)
        return false;

    if (!apply_resource_grants(runtime, collector, obj_get(action, "resources"), payload))
        return false;

    if (json_bool(action, "deactivate", true))
    {
        pickup->active = false;
        slayer3d_properties_set_bool(pickup->props, json_string(action, "available_property", "pickup_available"),
                                     false);
        const float respawn_seconds = json_float(action, "respawn_seconds", 0.0f);
        if (respawn_seconds > 0.0f)
        {
            slayer3d_properties_set_float(
                pickup->props, json_string(action, "timer_property", "pickup_respawn_remaining"), respawn_seconds);
        }
    }

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", collector->name != NULL ? collector->name : "");
        slayer3d_properties_set_string(event_payload, "pickup_actor_name", pickup->name != NULL ? pickup->name : "");
        emit_optional_signal(runtime, action, "on_collected", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool execute_resource_station_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    slayer3d_registered_actor *station = slayer3d_game_data_find_actor(runtime, json_string(action, "station", NULL));
    const char *station_from_payload = json_string(action, "station_from_payload", NULL);
    if (station == NULL && station_from_payload != NULL)
        station = actor_from_payload_key(runtime, payload, station_from_payload);
    if (target == NULL || station == NULL)
        return false;

    const char *cooldown_property = json_string(action, "cooldown_property", "cooldown");
    const char *charges_property = json_string(action, "charges_property", "charges");
    const float cooldown = slayer3d_properties_get_float(station->props, cooldown_property, 0.0f);
    const float charges = actor_numeric_property(station, charges_property, 1.0f);
    const bool use_charge = json_bool(action, "consume_charge", true);
    const bool success = cooldown <= 0.0f && (!use_charge || charges > 0.0f);
    if (success)
    {
        if (!apply_resource_grants(runtime, target, obj_get(action, "resources"), payload))
            return false;
        if (use_charge)
            set_actor_numeric_property(station, charges_property, SDL_max(charges - 1.0f, 0.0f));
        slayer3d_properties_set_float(station->props, cooldown_property, json_float(action, "cooldown", cooldown));
    }

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", target->name != NULL ? target->name : "");
        slayer3d_properties_set_string(event_payload, "station_actor_name", station->name != NULL ? station->name : "");
        slayer3d_properties_set_float(event_payload, "charges",
                                      actor_numeric_property(station, charges_property, 0.0f));
        slayer3d_properties_set_float(event_payload, "cooldown",
                                      slayer3d_properties_get_float(station->props, cooldown_property, 0.0f));
        slayer3d_properties_set_bool(event_payload, "success", success);
        emit_optional_signal(runtime, action, success ? "on_success" : "on_failure", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool execute_status_effect_apply_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                               const slayer3d_properties *payload)
{
    slayer3d_registered_actor *target = action_target_actor(runtime, action, payload);
    const char *property = json_string(action, "property", NULL);
    const char *duration_property = json_string(action, "duration_property", NULL);
    char duration_buffer[128];
    if (duration_property == NULL && property != NULL)
    {
        SDL_snprintf(duration_buffer, sizeof(duration_buffer), "%s_remaining", property);
        duration_property = duration_buffer;
    }

    slayer3d_value value;
    float duration = 0.0f;
    if (target == NULL || property == NULL || property[0] == '\0' ||
        !json_scalar_to_value(obj_get(action, "value"), &value) ||
        !resource_numeric_value(action, payload, "duration", "duration_from_payload", &duration))
    {
        return false;
    }

    if (!set_property_from_value(target->props, property, &value))
        return false;
    slayer3d_properties_set_float(target->props, duration_property, SDL_max(duration, 0.0f));
    const char *active_property = json_string(action, "active_property", NULL);
    if (active_property != NULL && active_property[0] != '\0')
        slayer3d_properties_set_bool(target->props, active_property, duration > 0.0f);

    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload != NULL)
    {
        slayer3d_properties_set_string(event_payload, "actor_name", target->name != NULL ? target->name : "");
        slayer3d_properties_set_string(event_payload, "property", property);
        slayer3d_properties_set_float(event_payload, "duration", duration);
        emit_optional_signal(runtime, action, "on_apply", event_payload);
        slayer3d_properties_destroy(event_payload);
    }
    return true;
}

static bool actor_matches_weapon_target(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *actor, const slayer3d_registered_actor *source,
                                        yyjson_val *action, const char *tag, bool exclude_source)
{
    return actor_matches_target_filter(runtime, actor, source, action, NULL, tag, exclude_source);
}

static bool ray_sphere_intersection(slayer3d_vec3 origin, slayer3d_vec3 direction, slayer3d_vec3 center, float radius,
                                    float max_distance, float *out_distance)
{
    const slayer3d_vec3 oc = slayer3d_vec3_make(origin.x - center.x, origin.y - center.y, origin.z - center.z);
    const float b = oc.x * direction.x + oc.y * direction.y + oc.z * direction.z;
    const float c = slayer3d_vec3_length_squared(oc) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return false;
    const float sqrt_discriminant = SDL_sqrtf(discriminant);
    float t = -b - sqrt_discriminant;
    if (t < 0.0f)
        t = -b + sqrt_discriminant;
    if (t < 0.0f || t > max_distance)
        return false;
    if (out_distance != NULL)
        *out_distance = t;
    return true;
}

static slayer3d_registered_actor *find_hitscan_actor_target(slayer3d_game_data_runtime *runtime,
                                                            const slayer3d_registered_actor *source, yyjson_val *action,
                                                            slayer3d_vec3 origin, slayer3d_vec3 direction,
                                                            float max_distance, float *out_distance)
{
    const char *tag = json_string(action, "target_tag", json_string(action, "hit_tag", NULL));
    const bool exclude_source = json_bool(action, "exclude_source", true);
    float best_distance = max_distance;
    slayer3d_registered_actor *best = NULL;

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (!actor_matches_weapon_target(runtime, actor, source, action, tag, exclude_source))
            continue;
        const float radius =
            SDL_max(weapon_actor_numeric_property(
                        actor, "hit_radius",
                        weapon_actor_numeric_property(actor, "radius", json_float(action, "hit_radius", 0.5f))),
                    0.001f);
        float distance = 0.0f;
        if (ray_sphere_intersection(origin, direction, actor->position, radius, best_distance, &distance))
        {
            best = actor;
            best_distance = distance;
        }
    }

    for (int pool_index = 0; runtime != NULL && pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (!actor_pool_actor_is_active(pool, actor, actor_index) ||
                !actor_matches_weapon_target(runtime, actor, source, action, tag, exclude_source))
            {
                continue;
            }
            const float radius =
                SDL_max(weapon_actor_numeric_property(
                            actor, "hit_radius",
                            weapon_actor_numeric_property(actor, "radius", json_float(action, "hit_radius", 0.5f))),
                        0.001f);
            float distance = 0.0f;
            if (ray_sphere_intersection(origin, direction, actor->position, radius, best_distance, &distance))
            {
                best = actor;
                best_distance = distance;
            }
        }
    }

    if (out_distance != NULL)
        *out_distance = best_distance;
    return best;
}

static slayer3d_vec3 weapon_direction_from_action(const slayer3d_registered_actor *source, yyjson_val *action)
{
    const slayer3d_vec3 fallback = json_vec3_value(obj_get(action, "direction"), slayer3d_vec3_make(0.0f, 0.0f, -1.0f));
    const char *direction_property = json_string(action, "direction_from_property", "camera_forward");
    slayer3d_vec3 direction = direction_property != NULL && source != NULL
                                  ? slayer3d_properties_get_vec3(source->props, direction_property, fallback)
                                  : fallback;
    if (slayer3d_vec3_length_squared(direction) <= 0.000001f)
        direction = slayer3d_vec3_make(0.0f, 0.0f, -1.0f);
    return slayer3d_vec3_normalize(direction);
}

static slayer3d_properties *weapon_hitscan_payload(const slayer3d_registered_actor *source,
                                                   const slayer3d_registered_actor *hit, slayer3d_vec3 origin,
                                                   slayer3d_vec3 direction, slayer3d_vec3 hit_position, float distance,
                                                   bool wall_hit,
                                                   const slayer3d_game_data_brush_trace_result *brush_hit)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "actor_name", hit != NULL && hit->name != NULL ? hit->name : "");
    slayer3d_properties_set_string(payload, "hit_actor_name", hit != NULL && hit->name != NULL ? hit->name : "");
    slayer3d_properties_set_vec3(payload, "origin", origin);
    slayer3d_properties_set_vec3(payload, "direction", direction);
    slayer3d_properties_set_vec3(payload, "hit_position", hit_position);
    slayer3d_properties_set_float(payload, "hit_distance", distance);
    slayer3d_properties_set_bool(payload, "hit_actor", hit != NULL);
    slayer3d_properties_set_bool(payload, "hit_wall", wall_hit);
    slayer3d_properties_set_bool(payload, "hit_brush", brush_hit != NULL && brush_hit->hit);
    slayer3d_properties_set_string(payload, "hit_brush_world",
                                   brush_hit != NULL && brush_hit->world_name != NULL ? brush_hit->world_name : "");
    slayer3d_properties_set_string(payload, "hit_brush_name",
                                   brush_hit != NULL && brush_hit->brush_name != NULL ? brush_hit->brush_name : "");
    slayer3d_properties_set_string(
        payload, "hit_material", brush_hit != NULL && brush_hit->material_name != NULL ? brush_hit->material_name : "");
    slayer3d_properties_set_vec3(payload, "hit_normal",
                                 brush_hit != NULL ? brush_hit->normal : slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_int(payload, "hit_contents",
                                brush_hit != NULL ? (int)SDL_min(brush_hit->contents, (unsigned int)SDL_MAX_SINT32)
                                                  : 0);
    slayer3d_properties_set_int(payload, "hit_surface_flags",
                                brush_hit != NULL ? (int)SDL_min(brush_hit->surface_flags, (unsigned int)SDL_MAX_SINT32)
                                                  : 0);
    return payload;
}

static bool execute_weapon_hitscan_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                          const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source = action_target_actor(runtime, action, payload);
    if (source == NULL || !runtime_actor_is_active(runtime, source))
        return false;
    if (!weapon_fire_prepare(runtime, action, source))
        return true;

    const float range = SDL_max(json_float(action, "range", 64.0f), 0.0f);
    slayer3d_vec3 origin = actor_spawn_position_from_action(runtime, action, payload, source->position);
    slayer3d_vec3 direction = weapon_direction_from_action(source, action);
    float wall_distance = range;
    bool wall_hit = false;
    slayer3d_vec3 hit_position = slayer3d_vec3_make(origin.x + direction.x * range, origin.y + direction.y * range,
                                                    origin.z + direction.z * range);

    const sector_level_runtime *level = find_sector_level_runtime(runtime, json_string(action, "sector_level", NULL));
    slayer3d_game_data_brush_trace_result brush_result;
    SDL_zero(brush_result);
    bool has_brush_result = false;
    if (level != NULL)
    {
        const slayer3d_level_trace_result trace =
            slayer3d_level_trace_point(&level->lightmapped, level->sectors, origin, direction, range);
        wall_distance = SDL_clamp(trace.fraction, 0.0f, 1.0f) * range;
        wall_hit = trace.hit;
        hit_position = trace.end_point;
    }
    if (obj_get(action, "brush_contents_mask") != NULL || json_bool(action, "trace_brush_worlds", false))
    {
        slayer3d_game_data_brush_trace_desc brush_trace;
        SDL_zero(brush_trace);
        brush_trace.start = origin;
        brush_trace.end = slayer3d_vec3_make(origin.x + direction.x * range, origin.y + direction.y * range,
                                             origin.z + direction.z * range);
        brush_trace.shape = SLAYER3D_GAME_DATA_BRUSH_TRACE_POINT;
        brush_trace.contents_mask = brush_flags_from_json(
            obj_get(action, "brush_contents_mask"), brush_content_flag_from_string,
            SLAYER3D_GAME_DATA_BRUSH_CONTENT_SOLID | SLAYER3D_GAME_DATA_BRUSH_CONTENT_PROJECTILE_CLIP);
        if (slayer3d_game_data_trace_active_brush_worlds(runtime, &brush_trace, &brush_result) && brush_result.hit)
        {
            const float brush_distance = SDL_clamp(brush_result.fraction, 0.0f, 1.0f) * range;
            if (brush_distance < wall_distance)
            {
                wall_distance = brush_distance;
                wall_hit = true;
                hit_position = brush_result.end_position;
                has_brush_result = true;
            }
        }
    }

    float actor_distance = wall_distance;
    slayer3d_registered_actor *hit_actor =
        find_hitscan_actor_target(runtime, source, action, origin, direction, wall_distance, &actor_distance);
    if (hit_actor != NULL)
    {
        hit_position =
            slayer3d_vec3_make(origin.x + direction.x * actor_distance, origin.y + direction.y * actor_distance,
                               origin.z + direction.z * actor_distance);
        wall_hit = false;
    }

    weapon_fire_commit(runtime, action, source);
    slayer3d_properties *hit_payload = weapon_hitscan_payload(
        source, hit_actor, origin, direction, hit_position, hit_actor != NULL ? actor_distance : wall_distance,
        wall_hit, has_brush_result && hit_actor == NULL ? &brush_result : NULL);
    const bool ok =
        execute_optional_action_array(runtime,
                                      hit_actor != NULL || wall_hit || json_bool(action, "run_actions_on_miss", false)
                                          ? obj_get(action, "actions")
                                          : obj_get(action, "miss_actions"),
                                      hit_payload);
    slayer3d_properties_destroy(hit_payload);
    return ok;
}

static void weapon_complete_reload(slayer3d_registered_actor *actor, yyjson_val *json)
{
    if (actor == NULL || json == NULL)
        return;

    const char *clip_property = json_string(json, "clip_property", "clip");
    const char *clip_size_property = json_string(json, "clip_size_property", "clip_size");
    const char *reserve_property = json_string(json, "reserve_property", "ammo_reserve");
    const char *pending_property = json_string(json, "reload_pending_property", "reload_pending");
    const char *timer_property = json_string(json, "reload_timer_property", "reload_timer");
    const float clip_size =
        SDL_max(json_float(json, "clip_size", weapon_actor_numeric_property(actor, clip_size_property, 0.0f)), 0.0f);
    const float clip = SDL_clamp(weapon_actor_numeric_property(actor, clip_property, 0.0f), 0.0f, clip_size);
    const bool consume_reserve = json_bool(json, "consume_reserve", true);
    const float reserve =
        consume_reserve ? SDL_max(weapon_actor_numeric_property(actor, reserve_property, 0.0f), 0.0f) : clip_size;
    const float needed = SDL_max(clip_size - clip, 0.0f);
    const float loaded = consume_reserve ? SDL_min(needed, reserve) : needed;
    weapon_set_actor_numeric_property(actor, clip_property, clip + loaded);
    if (consume_reserve)
        weapon_set_actor_numeric_property(actor, reserve_property, reserve - loaded);
    slayer3d_properties_set_bool(actor->props, pending_property, false);
    slayer3d_properties_set_float(actor->props, timer_property, 0.0f);
}

static bool execute_weapon_reload_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                         const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
    if (actor == NULL)
        return false;

    const char *clip_property = json_string(action, "clip_property", "clip");
    const char *clip_size_property = json_string(action, "clip_size_property", "clip_size");
    const char *reserve_property = json_string(action, "reserve_property", "ammo_reserve");
    const char *pending_property = json_string(action, "reload_pending_property", "reload_pending");
    const char *timer_property = json_string(action, "reload_timer_property", "reload_timer");
    const float clip_size =
        SDL_max(json_float(action, "clip_size", weapon_actor_numeric_property(actor, clip_size_property, 0.0f)), 0.0f);
    const float clip = SDL_clamp(weapon_actor_numeric_property(actor, clip_property, 0.0f), 0.0f, clip_size);
    const bool consume_reserve = json_bool(action, "consume_reserve", true);
    const float reserve = consume_reserve ? weapon_actor_numeric_property(actor, reserve_property, 0.0f) : clip_size;
    if (clip >= clip_size || reserve <= 0.0f)
    {
        weapon_emit_signal(runtime, action, "on_failure", actor, WEAPON_FIRE_EMPTY);
        return true;
    }

    const float reload_seconds = SDL_max(json_float(action, "reload_seconds", 0.0f), 0.0f);
    slayer3d_properties_set_bool(actor->props, pending_property, true);
    slayer3d_properties_set_float(actor->props, timer_property, reload_seconds);
    weapon_emit_signal(runtime, action, "on_reload", actor, WEAPON_FIRE_RELOADING);
    if (reload_seconds <= 0.0f)
        weapon_complete_reload(actor, action);
    return true;
}

static bool execute_projectile_fire_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                           const slayer3d_properties *payload)
{
    return execute_projectile_fire_action_for_actor(runtime, action, payload, NULL);
}

static bool grid_spawn_rule_matches(yyjson_val *rule, char glyph)
{
    const char *rule_glyph = json_string(rule, "glyph", NULL);
    return rule_glyph != NULL && rule_glyph[0] == glyph && rule_glyph[1] == '\0';
}

static bool execute_grid_spawn_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const grid_map_runtime *map = find_grid_map(runtime, json_string(action, "map", NULL));
    yyjson_val *spawns = obj_get(action, "spawns");
    if (runtime == NULL || map == NULL || !yyjson_is_arr(spawns))
        return false;

    bool ok = true;
    int spawned_count = 0;
    for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, i);
        grid_actor_index_clear(runtime, map, json_string(rule, "pool", NULL));
    }
    for (int row = 0; row < map->height; ++row)
    {
        for (int col = 0; col < map->width; ++col)
        {
            const char glyph = grid_map_cell(map, col, row);
            yyjson_val *rule = NULL;
            for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
            {
                yyjson_val *candidate = yyjson_arr_get(spawns, i);
                if (grid_spawn_rule_matches(candidate, glyph))
                {
                    rule = candidate;
                    break;
                }
            }
            if (rule == NULL)
                continue;

            actor_pool_runtime *pool = find_actor_pool(runtime, json_string(rule, "pool", NULL));
            actor_pool_note_spawn_attempt(pool);
            int actor_index = -1;
            slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
            if (pool == NULL || actor == NULL || actor_index < 0)
            {
                actor_pool_note_spawn_failure(pool, "exhausted");
                ok = false;
                continue;
            }

            actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
            if (!initialize_pooled_actor(pool, actor, actor_index, true))
            {
                actor_pool_note_spawn_failure(pool, "initialize_failed");
                ok = false;
                continue;
            }
            if (pool->spawn_generations != NULL)
            {
                pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
                slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                            (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
            }

            slayer3d_vec3 position;
            if (!grid_map_cell_to_world(map, col, row, &position))
            {
                actor_pool_note_spawn_failure(pool, "invalid_cell");
                ok = false;
                continue;
            }
            position.z = json_float(rule, "z", json_float(action, "z", position.z));
            actor_set_position(actor, position);
            slayer3d_properties_set_string(actor->props, "grid_map", map->name);
            slayer3d_properties_set_int(actor->props, "grid_col", col);
            slayer3d_properties_set_int(actor->props, "grid_row", row);
            char glyph_text[2] = {glyph, '\0'};
            slayer3d_properties_set_string(actor->props, "grid_glyph", glyph_text);
            apply_actor_spawn_properties(actor, obj_get(action, "properties"));
            apply_actor_spawn_properties(actor, obj_get(rule, "properties"));
            if (!grid_actor_index_register(runtime, map, pool != NULL ? pool->name : NULL, actor, col, row))
                ok = false;
            actor_pool_note_spawn_success(runtime, pool);
            spawned_count++;
        }
    }

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, spawned_count);
    return ok;
}

static bool grid_run_axis_is_y(yyjson_val *action, yyjson_val *rule)
{
    const char *axis = json_string(rule, "axis", json_string(action, "axis", "x"));
    return axis != NULL &&
           (SDL_strcmp(axis, "y") == 0 || SDL_strcmp(axis, "vertical") == 0 || SDL_strcmp(axis, "column") == 0);
}

static bool spawn_grid_run_actor(slayer3d_game_data_runtime *runtime, const grid_map_runtime *map, yyjson_val *action,
                                 yyjson_val *rule, int start_col, int start_row, int end_col, int end_row)
{
    actor_pool_runtime *pool = find_actor_pool(runtime, json_string(rule, "pool", NULL));
    actor_pool_note_spawn_attempt(pool);
    int actor_index = -1;
    slayer3d_registered_actor *actor = actor_pool_allocate(runtime, pool, &actor_index);
    if (pool == NULL || actor == NULL || actor_index < 0)
    {
        actor_pool_note_spawn_failure(pool, "exhausted");
        return false;
    }

    actor_pool_set_lifecycle_state(pool, actor, actor_index, ACTOR_LIFECYCLE_SPAWNING);
    if (!initialize_pooled_actor(pool, actor, actor_index, true))
    {
        actor_pool_note_spawn_failure(pool, "initialize_failed");
        return false;
    }
    if (pool->spawn_generations != NULL)
    {
        pool->spawn_generations[actor_index] = ++pool->spawn_generation_counter;
        slayer3d_properties_set_int(actor->props, "pool_spawn_generation",
                                    (int)SDL_min(pool->spawn_generations[actor_index], (Uint64)SDL_MAX_SINT32));
    }

    slayer3d_vec3 start_position;
    slayer3d_vec3 end_position;
    if (!grid_map_cell_to_world(map, start_col, start_row, &start_position) ||
        !grid_map_cell_to_world(map, end_col, end_row, &end_position))
    {
        actor_pool_note_spawn_failure(pool, "invalid_cell");
        return false;
    }

    slayer3d_vec3 position =
        slayer3d_vec3_make((start_position.x + end_position.x) * 0.5f, (start_position.y + end_position.y) * 0.5f,
                           (start_position.z + end_position.z) * 0.5f);
    position.z = json_float(rule, "z", json_float(action, "z", position.z));
    actor_set_position(actor, position);

    const bool axis_y = grid_run_axis_is_y(action, rule);
    const int run_length = axis_y ? SDL_abs(end_row - start_row) + 1 : SDL_abs(end_col - start_col) + 1;
    const float depth = json_float(rule, "depth", json_float(action, "depth", 0.25f));
    const float inset = SDL_max(0.0f, json_float(rule, "inset", json_float(action, "inset", 0.0f)));
    const float run_width = axis_y ? SDL_max(0.001f, map->cell_width - inset * 2.0f)
                                   : SDL_max(0.001f, map->cell_width * (float)run_length - inset * 2.0f);
    const float run_height = axis_y ? SDL_max(0.001f, map->cell_height * (float)run_length - inset * 2.0f)
                                    : SDL_max(0.001f, map->cell_height - inset * 2.0f);
    const slayer3d_vec3 run_size =
        json_vec3(rule, "size", json_vec3(action, "size", slayer3d_vec3_make(run_width, run_height, depth)));

    char glyph_text[2] = {grid_map_cell(map, start_col, start_row), '\0'};
    slayer3d_properties_set_string(actor->props, "grid_map", map->name);
    slayer3d_properties_set_int(actor->props, "grid_col", start_col);
    slayer3d_properties_set_int(actor->props, "grid_row", start_row);
    slayer3d_properties_set_string(actor->props, "grid_glyph", glyph_text);
    slayer3d_properties_set_string(actor->props, "grid_run_axis", axis_y ? "y" : "x");
    slayer3d_properties_set_int(actor->props, "grid_run_start_col", start_col);
    slayer3d_properties_set_int(actor->props, "grid_run_start_row", start_row);
    slayer3d_properties_set_int(actor->props, "grid_run_end_col", end_col);
    slayer3d_properties_set_int(actor->props, "grid_run_end_row", end_row);
    slayer3d_properties_set_int(actor->props, "grid_run_length", run_length);
    slayer3d_properties_set_vec3(actor->props, "grid_run_size", run_size);
    apply_actor_spawn_properties(actor, obj_get(action, "properties"));
    apply_actor_spawn_properties(actor, obj_get(rule, "properties"));

    for (int row = SDL_min(start_row, end_row); row <= SDL_max(start_row, end_row); ++row)
    {
        for (int col = SDL_min(start_col, end_col); col <= SDL_max(start_col, end_col); ++col)
            (void)grid_actor_index_register(runtime, map, pool->name, actor, col, row);
    }
    actor_pool_note_spawn_success(runtime, pool);
    return true;
}

static bool execute_grid_spawn_runs_from_glyphs_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    const grid_map_runtime *map = find_grid_map(runtime, json_string(action, "map", NULL));
    yyjson_val *spawns = obj_get(action, "spawns");
    if (runtime == NULL || map == NULL || !yyjson_is_arr(spawns))
        return false;

    bool ok = true;
    int spawned_count = 0;
    for (size_t i = 0; i < yyjson_arr_size(spawns); ++i)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, i);
        grid_actor_index_clear(runtime, map, json_string(rule, "pool", NULL));
    }

    for (size_t rule_index = 0; rule_index < yyjson_arr_size(spawns); ++rule_index)
    {
        yyjson_val *rule = yyjson_arr_get(spawns, rule_index);
        const bool axis_y = grid_run_axis_is_y(action, rule);
        if (axis_y)
        {
            for (int col = 0; col < map->width; ++col)
            {
                int row = 0;
                while (row < map->height)
                {
                    if (!grid_spawn_rule_matches(rule, grid_map_cell(map, col, row)))
                    {
                        ++row;
                        continue;
                    }
                    const int start_row = row;
                    while (row + 1 < map->height && grid_spawn_rule_matches(rule, grid_map_cell(map, col, row + 1)))
                        ++row;
                    if (spawn_grid_run_actor(runtime, map, action, rule, col, start_row, col, row))
                        ++spawned_count;
                    else
                        ok = false;
                    ++row;
                }
            }
        }
        else
        {
            for (int row = 0; row < map->height; ++row)
            {
                int col = 0;
                while (col < map->width)
                {
                    if (!grid_spawn_rule_matches(rule, grid_map_cell(map, col, row)))
                    {
                        ++col;
                        continue;
                    }
                    const int start_col = col;
                    while (col + 1 < map->width && grid_spawn_rule_matches(rule, grid_map_cell(map, col + 1, row)))
                        ++col;
                    if (spawn_grid_run_actor(runtime, map, action, rule, start_col, row, col, row))
                        ++spawned_count;
                    else
                        ok = false;
                    ++col;
                }
            }
        }
    }

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, spawned_count);
    return ok;
}

static bool execute_grid_pickup_layer_reset_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    grid_pickup_layer_runtime *layer = find_grid_pickup_layer(runtime, json_string(action, "layer", NULL));
    if (layer == NULL || !grid_pickup_layer_reset(runtime, layer))
        return false;

    const char *count_key = json_string(action, "output_count_key", NULL);
    if (count_key != NULL && runtime != NULL && runtime->scene_state != NULL)
        slayer3d_properties_set_int(runtime->scene_state, count_key, layer->active_count);
    return true;
}

static yyjson_val *actor_component_json(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *actor, const char *type)
{
    if (runtime == NULL || actor == NULL || type == NULL)
        return NULL;

    yyjson_val *entity = find_entity_json(runtime, actor->name);
    if (entity == NULL)
    {
        int actor_index = -1;
        const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor->name, &actor_index);
        if (pool != NULL && actor_index >= 0)
            entity = pool->archetype_json;
    }

    yyjson_val *components = obj_get(entity, "components");
    for (size_t i = 0; yyjson_is_arr(components) && i < yyjson_arr_size(components); ++i)
    {
        yyjson_val *component = yyjson_arr_get(components, i);
        if (SDL_strcmp(json_string(component, "type", ""), type) != 0)
            continue;
        yyjson_val *enabled_if = obj_get(component, "enabled_if");
        if (enabled_if != NULL && !eval_data_condition(runtime, enabled_if, NULL))
            continue;
        return component;
    }
    return NULL;
}

static float json_float_override(yyjson_val *primary, yyjson_val *fallback_json, const char *key, float fallback)
{
    yyjson_val *value = obj_get(primary, key);
    if (yyjson_is_num(value))
        return (float)yyjson_get_num(value);
    return json_float(fallback_json, key, fallback);
}

static bool actor_has_authored_tag(const slayer3d_game_data_runtime *runtime, const slayer3d_registered_actor *actor,
                                   const char *tag)
{
    if (tag == NULL || tag[0] == '\0')
        return true;
    yyjson_val *entity = find_entity_json(runtime, actor != NULL ? actor->name : NULL);
    if (entity_json_has_tags(entity, &tag, 1))
        return true;
    int actor_index = -1;
    const actor_pool_runtime *pool =
        find_actor_pool_for_actor_const(runtime, actor != NULL ? actor->name : NULL, &actor_index);
    return pool != NULL && actor_index >= 0 && entity_json_has_tags(pool->archetype_json, &tag, 1);
}

static yyjson_val *target_filter_json(yyjson_val *json)
{
    yyjson_val *filter = obj_get(json, "target_filter");
    return yyjson_is_obj(filter) ? filter : json;
}

static const char *target_filter_string(yyjson_val *json, const char *key, const char *fallback)
{
    yyjson_val *filter = target_filter_json(json);
    const char *value = json_string(filter, key, NULL);
    if (value != NULL)
        return value;
    return filter != json ? json_string(json, key, fallback) : fallback;
}

static bool target_filter_bool(yyjson_val *json, const char *key, bool fallback)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *value = obj_get(filter, key);
    if (yyjson_is_bool(value))
        return yyjson_get_bool(value);
    return filter != json ? json_bool(json, key, fallback) : fallback;
}

static bool target_filter_string_array_contains(yyjson_val *json, const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0')
        return false;
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *array = obj_get(filter, key);
    if (array == NULL && filter != json)
        array = obj_get(json, key);
    if (yyjson_is_str(array))
        return SDL_strcmp(yyjson_get_str(array), value) == 0;
    for (size_t i = 0; yyjson_is_arr(array) && i < yyjson_arr_size(array); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(array, i);
        if (yyjson_is_str(entry) && SDL_strcmp(yyjson_get_str(entry), value) == 0)
            return true;
    }
    return false;
}

static bool target_filter_actor_has_any_tag(const slayer3d_game_data_runtime *runtime,
                                            const slayer3d_registered_actor *actor, yyjson_val *json, const char *key)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *tags = obj_get(filter, key);
    if (tags == NULL && filter != json)
        tags = obj_get(json, key);
    if (yyjson_is_str(tags))
        return actor_has_authored_tag(runtime, actor, yyjson_get_str(tags));
    for (size_t i = 0; yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (yyjson_is_str(tag) && actor_has_authored_tag(runtime, actor, yyjson_get_str(tag)))
            return true;
    }
    return false;
}

static bool target_filter_actor_has_blocked_tag(const slayer3d_game_data_runtime *runtime,
                                                const slayer3d_registered_actor *actor, yyjson_val *json,
                                                const char *key)
{
    yyjson_val *filter = target_filter_json(json);
    yyjson_val *tags = obj_get(filter, key);
    if (tags == NULL && filter != json)
        tags = obj_get(json, key);
    if (yyjson_is_str(tags))
        return actor_has_authored_tag(runtime, actor, yyjson_get_str(tags));
    for (size_t i = 0; yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (yyjson_is_str(tag) && actor_has_authored_tag(runtime, actor, yyjson_get_str(tag)))
            return true;
    }
    return false;
}

static const char *actor_faction(const slayer3d_registered_actor *actor, const char *property)
{
    return actor != NULL && property != NULL && property[0] != '\0'
               ? slayer3d_properties_get_string(actor->props, property, "")
               : "";
}

static const char *faction_relationship(const slayer3d_game_data_runtime *runtime, const char *source_faction,
                                        const char *target_faction)
{
    if (source_faction == NULL || source_faction[0] == '\0' || target_faction == NULL || target_faction[0] == '\0')
        return "neutral";

    yyjson_val *factions = obj_get(runtime_root(runtime), "factions");
    yyjson_val *source = obj_get(factions, source_faction);
    const char *authored = json_string(source, target_faction, NULL);
    if (authored != NULL)
        return authored;
    authored = json_string(source, "default", NULL);
    if (authored != NULL)
        return authored;
    authored = json_string(factions, "default_relationship", NULL);
    if (authored != NULL)
        return authored;
    return SDL_strcmp(source_faction, target_faction) == 0 ? "friendly" : "hostile";
}

static slayer3d_registered_actor *action_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, target_filter_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, target_filter_string(action, "source", NULL));
    return source;
}

static bool actor_matches_target_filter(const slayer3d_game_data_runtime *runtime,
                                        const slayer3d_registered_actor *target,
                                        const slayer3d_registered_actor *source, yyjson_val *json,
                                        const slayer3d_properties *payload, const char *fallback_tag,
                                        bool fallback_exclude_source)
{
    if (target == NULL || !target->active)
        return false;
    const char *tag = target_filter_string(json, "target_tag", fallback_tag);
    if (tag == NULL)
        tag = target_filter_string(json, "affected_tag", NULL);
    if (tag == NULL)
        tag = target_filter_string(json, "hit_tag", NULL);
    if (tag != NULL && tag[0] != '\0' && !actor_has_authored_tag(runtime, target, tag))
        return false;
    if (target_filter_actor_has_any_tag(runtime, target, json, "include_tags") == false &&
        (obj_get(target_filter_json(json), "include_tags") != NULL || obj_get(json, "include_tags") != NULL))
        return false;
    if (target_filter_actor_has_blocked_tag(runtime, target, json, "exclude_tags"))
        return false;

    const bool exclude_source =
        target_filter_bool(json, "exclude_source", target_filter_bool(json, "exclude_self", fallback_exclude_source));
    if (exclude_source && source != NULL && target == source)
        return false;

    const char *owner_property =
        target_filter_string(json, "owner_property", target_filter_string(json, "owner_actor_property", "owner"));
    const bool exclude_owner = target_filter_bool(json, "exclude_owner", false);
    if (exclude_owner && source != NULL && owner_property != NULL && owner_property[0] != '\0')
    {
        const char *source_owner = slayer3d_properties_get_string(source->props, owner_property, NULL);
        if (source_owner != NULL && target->name != NULL && SDL_strcmp(source_owner, target->name) == 0)
            return false;
        const char *target_owner = slayer3d_properties_get_string(target->props, owner_property, NULL);
        if (target_owner != NULL && source->name != NULL && SDL_strcmp(target_owner, source->name) == 0)
            return false;
    }

    const char *faction_property = target_filter_string(json, "faction_property", "faction");
    const char *source_faction_property = target_filter_string(json, "source_faction_property", faction_property);
    const char *target_faction = actor_faction(target, faction_property);
    const char *source_faction = target_filter_string(json, "source_faction", NULL);
    const char *source_faction_from_payload = target_filter_string(json, "source_faction_from_payload", NULL);
    if (source_faction_from_payload != NULL && payload != NULL)
        source_faction = slayer3d_properties_get_string(payload, source_faction_from_payload, source_faction);
    if ((source_faction == NULL || source_faction[0] == '\0') && source != NULL)
        source_faction = actor_faction(source, source_faction_property);

    const char *required_target_faction = target_filter_string(json, "target_faction", NULL);
    if (required_target_faction != NULL && required_target_faction[0] != '\0' &&
        SDL_strcmp(target_faction, required_target_faction) != 0)
        return false;
    if (target_filter_string_array_contains(json, "include_factions", target_faction) == false &&
        (obj_get(target_filter_json(json), "include_factions") != NULL || obj_get(json, "include_factions") != NULL))
        return false;
    if (target_filter_string_array_contains(json, "exclude_factions", target_faction))
        return false;

    const char *relationship = target_filter_string(json, "relationship", NULL);
    if (relationship != NULL && relationship[0] != '\0' && SDL_strcmp(relationship, "any") != 0)
    {
        const char *actual = faction_relationship(runtime, source_faction, target_faction);
        if (SDL_strcmp(actual, relationship) != 0)
            return false;
    }
    return true;
}

static bool interaction_actor_in_front(const slayer3d_registered_actor *source, const slayer3d_registered_actor *target,
                                       yyjson_val *action, yyjson_val *component)
{
    if (source == NULL || target == NULL)
        return false;
    const float min_dot = SDL_clamp(json_float_override(action, component, "min_dot", 0.15f), -1.0f, 1.0f);
    if (min_dot <= -1.0f)
        return true;

    const char *yaw_property = json_string(action, "yaw_property", json_string(component, "yaw_property", "yaw"));
    const float yaw = slayer3d_properties_get_float(source->props, yaw_property, 0.0f);
    const slayer3d_vec3 forward = slayer3d_vec3_make(SDL_sinf(yaw), 0.0f, -SDL_cosf(yaw));
    slayer3d_vec3 to_target =
        slayer3d_vec3_make(target->position.x - source->position.x, 0.0f, target->position.z - source->position.z);
    const float len_sq = slayer3d_vec3_length_squared(to_target);
    if (len_sq <= 0.000001f)
        return true;
    to_target = slayer3d_vec3_normalize(to_target);
    return forward.x * to_target.x + forward.z * to_target.z >= min_dot;
}

static slayer3d_registered_actor *find_interactable_actor(slayer3d_game_data_runtime *runtime,
                                                          slayer3d_registered_actor *source, yyjson_val *action,
                                                          yyjson_val **out_component, float *out_distance)
{
    if (out_component != NULL)
        *out_component = NULL;
    if (out_distance != NULL)
        *out_distance = 0.0f;
    if (runtime == NULL || source == NULL)
        return NULL;

    const char *tag = json_string(action, "target_tag", json_string(action, "interactable_tag", NULL));
    slayer3d_registered_actor *best = NULL;
    yyjson_val *best_component = NULL;
    float best_distance_sq = 0.0f;

    sensor_actor_list candidates;
    SDL_zero(candidates);
    if (!collect_sensor_endpoint_actors(runtime, NULL, tag, &candidates))
    {
        sensor_actor_list_free(&candidates);
        return NULL;
    }

    if (tag == NULL)
    {
        sensor_actor_list_free(&candidates);
        yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
        for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
        {
            const char *entity_name = json_string(yyjson_arr_get(entities, i), "name", NULL);
            if (!active_scene_has_entity_internal(runtime, entity_name))
                continue;
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
            if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(&candidates, actor))
            {
                sensor_actor_list_free(&candidates);
                return NULL;
            }
        }
        for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
        {
            actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
            if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
                continue;
            for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
            {
                slayer3d_registered_actor *actor =
                    slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
                if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(&candidates, actor))
                {
                    sensor_actor_list_free(&candidates);
                    return NULL;
                }
            }
        }
    }

    for (int i = 0; i < candidates.count; ++i)
    {
        slayer3d_registered_actor *candidate = candidates.items[i];
        if (candidate == source || !actor_has_authored_tag(runtime, candidate, tag))
            continue;
        yyjson_val *component = actor_component_json(runtime, candidate, "interactable");
        if (component == NULL)
            continue;
        const float range = SDL_max(json_float_override(action, component, "range", 2.25f), 0.0f);
        const float dx = candidate->position.x - source->position.x;
        const float dy = candidate->position.y - source->position.y;
        const float dz = candidate->position.z - source->position.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > range * range || !interaction_actor_in_front(source, candidate, action, component))
            continue;
        if (best == NULL || distance_sq < best_distance_sq)
        {
            best = candidate;
            best_component = component;
            best_distance_sq = distance_sq;
        }
    }

    sensor_actor_list_free(&candidates);
    if (out_component != NULL)
        *out_component = best_component;
    if (out_distance != NULL)
        *out_distance = best != NULL ? SDL_sqrtf(best_distance_sq) : 0.0f;
    return best;
}

static const char *interaction_prompt_key(yyjson_val *component)
{
    return json_string(component, "prompt_key", json_string(component, "prompt", ""));
}

static bool interaction_requirement_met(slayer3d_registered_actor *source, yyjson_val *component)
{
    yyjson_val *requirement = obj_get(component, "requires");
    if (!yyjson_is_obj(requirement))
        return true;
    const char *property = json_string(requirement, "property", json_string(requirement, "resource", NULL));
    if (source == NULL || property == NULL || property[0] == '\0')
        return false;
    const float amount = SDL_max(json_float(requirement, "amount", 1.0f), 0.0f);
    return weapon_actor_numeric_property(source, property, 0.0f) + 0.0001f >= amount;
}

static void interaction_consume_requirement(slayer3d_registered_actor *source, yyjson_val *component)
{
    yyjson_val *requirement = obj_get(component, "requires");
    if (!yyjson_is_obj(requirement) || !json_bool(requirement, "consume", false))
        return;
    const char *property = json_string(requirement, "property", json_string(requirement, "resource", NULL));
    if (property == NULL || property[0] == '\0')
        return;
    const float amount = SDL_max(json_float(requirement, "amount", 1.0f), 0.0f);
    const float current = weapon_actor_numeric_property(source, property, 0.0f);
    weapon_set_actor_numeric_property(source, property, SDL_max(current - amount, 0.0f));
}

static slayer3d_properties *interaction_payload(const slayer3d_registered_actor *source,
                                                const slayer3d_registered_actor *interactable, yyjson_val *component,
                                                const char *status, float distance)
{
    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return NULL;
    slayer3d_properties_set_string(payload, "actor_name", source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_string(payload, "interactable_actor_name",
                                   interactable != NULL && interactable->name != NULL ? interactable->name : "");
    slayer3d_properties_set_string(payload, "target_actor_name",
                                   interactable != NULL && interactable->name != NULL ? interactable->name : "");
    slayer3d_properties_set_string(payload, "prompt_key", interaction_prompt_key(component));
    slayer3d_properties_set_string(payload, "interaction_status", status != NULL ? status : "success");
    slayer3d_properties_set_bool(payload, "locked", status != NULL && SDL_strcmp(status, "locked") == 0);
    slayer3d_properties_set_float(payload, "distance", distance);
    return payload;
}

static bool interaction_emit_or_run(slayer3d_game_data_runtime *runtime, yyjson_val *component, const char *actions_key,
                                    const char *signal_key, const slayer3d_properties *payload)
{
    yyjson_val *actions = obj_get(component, actions_key);
    if (yyjson_is_arr(actions))
        return execute_action_array(runtime, actions, payload);
    const int signal_id = action_signal_id(runtime, component, signal_key);
    if (signal_id < 0)
        return true;
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    if (bus == NULL)
        return false;
    slayer3d_signal_emit(bus, signal_id, payload);
    return true;
}

static bool execute_interaction_use_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                           const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "actor_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (!runtime_actor_is_active(runtime, source))
        return false;

    yyjson_val *component = NULL;
    float distance = 0.0f;
    slayer3d_registered_actor *target = find_interactable_actor(runtime, source, action, &component, &distance);
    if (target == NULL || component == NULL)
        return execute_optional_action_array(runtime, obj_get(action, "miss_actions"), NULL);

    const char *cooldown_property = json_string(component, "cooldown_property", NULL);
    if (cooldown_property != NULL && cooldown_property[0] != '\0' &&
        slayer3d_properties_get_float(target->props, cooldown_property, 0.0f) > 0.0f)
    {
        slayer3d_properties *event_payload = interaction_payload(source, target, component, "cooldown", distance);
        const bool ok = interaction_emit_or_run(runtime, component, "cooldown_actions", "on_cooldown", event_payload);
        slayer3d_properties_destroy(event_payload);
        return ok;
    }

    if (!interaction_requirement_met(source, component))
    {
        slayer3d_properties *event_payload = interaction_payload(source, target, component, "locked", distance);
        const bool ok = interaction_emit_or_run(runtime, component, "locked_actions", "on_locked", event_payload);
        slayer3d_properties_destroy(event_payload);
        return ok;
    }

    interaction_consume_requirement(source, component);
    const float cooldown = SDL_max(json_float(component, "cooldown", 0.0f), 0.0f);
    if (cooldown_property != NULL && cooldown_property[0] != '\0' && cooldown > 0.0f)
        slayer3d_properties_set_float(target->props, cooldown_property, cooldown);

    slayer3d_properties *event_payload = interaction_payload(source, target, component, "success", distance);
    const bool ok = interaction_emit_or_run(runtime, component, "actions", "signal", event_payload);
    slayer3d_properties_destroy(event_payload);
    return ok;
}

static slayer3d_registered_actor *effect_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "source", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "from", NULL));
    return source;
}

static bool collect_effect_targets(slayer3d_game_data_runtime *runtime, const char *tag, sensor_actor_list *out_list)
{
    if (runtime == NULL || out_list == NULL)
        return false;
    if (tag != NULL && tag[0] != '\0')
        return collect_sensor_endpoint_actors(runtime, NULL, tag, out_list);

    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        const char *entity_name = json_string(yyjson_arr_get(entities, i), "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        if (runtime_actor_is_active(runtime, actor) && !sensor_actor_list_add(out_list, actor))
            return false;
    }

    for (int pool_index = 0; pool_index < runtime->actor_pool_count; ++pool_index)
    {
        actor_pool_runtime *pool = &runtime->actor_pools[pool_index];
        if (!actor_pool_in_scene(pool, slayer3d_game_data_active_scene(runtime)))
            continue;
        for (int actor_index = 0; actor_index < pool->capacity; ++actor_index)
        {
            slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, pool->actor_names[actor_index]);
            if (actor_pool_actor_is_active(pool, actor, actor_index) && !sensor_actor_list_add(out_list, actor))
                return false;
        }
    }
    return true;
}

static float effect_explosion_falloff(yyjson_val *action, float distance)
{
    const float radius = SDL_max(json_float(action, "radius", 0.0f), 0.0f);
    const float inner_radius = SDL_clamp(json_float(action, "inner_radius", 0.0f), 0.0f, radius);
    if (distance <= inner_radius)
        return 1.0f;
    if (radius <= inner_radius)
        return distance <= radius ? 1.0f : 0.0f;
    const char *falloff = json_string(action, "falloff", "linear");
    if (falloff != NULL && (SDL_strcmp(falloff, "none") == 0 || SDL_strcmp(falloff, "constant") == 0))
        return 1.0f;
    return SDL_clamp(1.0f - ((distance - inner_radius) / (radius - inner_radius)), 0.0f, 1.0f);
}

static slayer3d_properties *effect_explosion_payload(const slayer3d_registered_actor *source,
                                                     const slayer3d_registered_actor *target, slayer3d_vec3 origin,
                                                     float distance, float radius, float falloff, float damage,
                                                     float impulse)
{
    slayer3d_properties *event_payload = slayer3d_properties_create();
    if (event_payload == NULL)
        return NULL;
    slayer3d_properties_set_string(event_payload, "actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "target_actor_name",
                                   target != NULL && target->name != NULL ? target->name : "");
    slayer3d_properties_set_string(event_payload, "source_actor_name",
                                   source != NULL && source->name != NULL ? source->name : "");
    slayer3d_properties_set_vec3(event_payload, "origin", origin);
    slayer3d_properties_set_float(event_payload, "distance", distance);
    slayer3d_properties_set_float(event_payload, "radius", radius);
    slayer3d_properties_set_float(event_payload, "falloff", falloff);
    slayer3d_properties_set_float(event_payload, "damage", damage);
    slayer3d_properties_set_float(event_payload, "amount", damage);
    slayer3d_properties_set_float(event_payload, "impulse", impulse);
    return event_payload;
}

static void effect_apply_impulse(slayer3d_registered_actor *actor, slayer3d_vec3 origin, float strength,
                                 const char *velocity_property)
{
    if (actor == NULL || strength <= 0.0f || velocity_property == NULL || velocity_property[0] == '\0')
        return;
    slayer3d_vec3 delta =
        slayer3d_vec3_make(actor->position.x - origin.x, actor->position.y - origin.y, actor->position.z - origin.z);
    if (slayer3d_vec3_length_squared(delta) <= 0.000001f)
        delta = slayer3d_vec3_make(0.0f, 1.0f, 0.0f);
    else
        delta = slayer3d_vec3_normalize(delta);
    const slayer3d_vec3 velocity =
        slayer3d_properties_get_vec3(actor->props, velocity_property, slayer3d_vec3_make(0.0f, 0.0f, 0.0f));
    slayer3d_properties_set_vec3(actor->props, velocity_property,
                                 slayer3d_vec3_make(velocity.x + delta.x * strength, velocity.y + delta.y * strength,
                                                    velocity.z + delta.z * strength));
}

static bool execute_effect_explosion_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                            const slayer3d_properties *payload)
{
    if (runtime == NULL || action == NULL)
        return false;

    const float radius = SDL_max(json_float(action, "radius", 0.0f), 0.0f);
    if (radius <= 0.0f)
        return true;
    slayer3d_registered_actor *source = effect_source_actor(runtime, action, payload);
    const slayer3d_vec3 fallback = source != NULL ? source->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 origin = actor_spawn_position_from_action(runtime, action, payload, fallback);
    const char *tag = json_string(action, "target_tag", json_string(action, "affected_tag", NULL));
    const bool exclude_source = json_bool(action, "exclude_source", true);
    const int max_targets = SDL_max(json_int(action, "max_targets", SDL_MAX_SINT32), 0);
    const float base_damage = SDL_max(json_float(action, "damage", json_float(action, "amount", 0.0f)), 0.0f);
    const float base_impulse = SDL_max(json_float(action, "impulse", 0.0f), 0.0f);
    const char *velocity_property = json_string(action, "velocity_property", "velocity");

    sensor_actor_list targets;
    SDL_zero(targets);
    if (!collect_effect_targets(runtime, tag, &targets))
    {
        sensor_actor_list_free(&targets);
        return false;
    }

    bool ok = true;
    int affected = 0;
    const float radius_sq = radius * radius;
    for (int i = 0; i < targets.count && affected < max_targets; ++i)
    {
        slayer3d_registered_actor *target = targets.items[i];
        if (!actor_matches_target_filter(runtime, target, source, action, payload, tag, exclude_source))
        {
            continue;
        }
        const float dx = target->position.x - origin.x;
        const float dy = target->position.y - origin.y;
        const float dz = target->position.z - origin.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > radius_sq)
            continue;
        const float distance = SDL_sqrtf(distance_sq);
        const float falloff = effect_explosion_falloff(action, distance);
        const float damage = base_damage * falloff;
        const float impulse = base_impulse * falloff;
        if (damage > 0.0f)
            ok = apply_combat_damage_to_actor(runtime, action, payload, target, damage) && ok;
        effect_apply_impulse(target, origin, impulse, velocity_property);

        slayer3d_properties *event_payload =
            effect_explosion_payload(source, target, origin, distance, radius, falloff, damage, impulse);
        ok = execute_optional_action_array(runtime, obj_get(action, "actions"), event_payload) && ok;
        emit_optional_signal(runtime, action, "on_hit", event_payload);
        slayer3d_properties_destroy(event_payload);
        ++affected;
    }

    sensor_actor_list_free(&targets);
    return ok;
}

static const char *sector_door_action_target_name(yyjson_val *action, const slayer3d_properties *payload)
{
    const char *target_from_payload = json_string(action, "target_from_payload", NULL);
    if (target_from_payload != NULL && payload != NULL)
    {
        const char *target = slayer3d_properties_get_string(payload, target_from_payload, NULL);
        if (target != NULL && target[0] != '\0')
            return target;
    }
    return json_string(action, "target", NULL);
}

static bool execute_sector_door_state_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                             const slayer3d_properties *payload, const char *kind)
{
    sector_door_runtime *door = find_sector_door(runtime, sector_door_action_target_name(action, payload));
    if (door == NULL || !sector_door_in_active_scene(runtime, door))
        return false;

    yyjson_val *auto_close = obj_get(action, "stay_open_seconds");
    if (auto_close == NULL)
        auto_close = obj_get(action, "auto_close_seconds");
    if (yyjson_is_num(auto_close))
        slayer3d_door_set_auto_close_delay(&door->door, (float)yyjson_get_num(auto_close));

    if (SDL_strcmp(kind, "open") == 0)
        return slayer3d_door_open(&door->door);
    if (SDL_strcmp(kind, "close") == 0)
        return slayer3d_door_close(&door->door);
    return slayer3d_door_toggle(&door->door);
}

static sector_door_runtime *find_interactable_sector_door(slayer3d_game_data_runtime *runtime,
                                                          const slayer3d_registered_actor *actor, yyjson_val *action)
{
    if (runtime == NULL || actor == NULL)
        return NULL;

    const float range = json_float(action, "range", 2.25f);
    const float min_dot = json_float(action, "min_dot", 0.15f);
    const float yaw = slayer3d_properties_get_float(actor->props, json_string(action, "yaw_property", "yaw"), 0.0f);
    sector_door_runtime *best = NULL;
    float best_distance = 0.0f;
    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        sector_door_runtime *door = &runtime->sector_doors[i];
        if (!sector_door_in_active_scene(runtime, door) ||
            !slayer3d_door_point_in_interaction_range(&door->door, actor->position, range) ||
            !sector_door_is_in_front(&door->door, actor->position, yaw, min_dot))
        {
            continue;
        }

        const float distance = sector_door_distance_sq_xz(&door->door, actor->position);
        if (best == NULL || distance < best_distance)
        {
            best = door;
            best_distance = distance;
        }
    }
    return best;
}

static bool execute_sector_door_interact_action(slayer3d_game_data_runtime *runtime, yyjson_val *action)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (actor == NULL)
        return false;

    sector_door_runtime *door = find_interactable_sector_door(runtime, actor, action);
    if (door == NULL)
        return true;

    slayer3d_properties *payload = slayer3d_properties_create();
    if (payload == NULL)
        return false;
    slayer3d_properties_set_string(payload, "actor_name", actor->name);
    slayer3d_properties_set_string(payload, "door_name", door->door.name != NULL ? door->door.name : "");
    slayer3d_properties_set_int(payload, "door_id", door->door.door_id);

    bool ok = true;
    yyjson_val *actions = obj_get(action, "actions");
    if (yyjson_is_arr(actions))
    {
        ok = execute_action_array(runtime, actions, payload);
    }
    else
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        ok = signal_id >= 0 && runtime_bus(runtime) != NULL;
        if (ok)
            slayer3d_signal_emit(runtime_bus(runtime), signal_id, payload);
    }
    slayer3d_properties_destroy(payload);
    return ok;
}

static slayer3d_registered_actor *noise_source_actor(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                                     const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source =
        actor_from_payload_key(runtime, payload, json_string(action, "source_from_payload", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "actor_from_payload", NULL));
    if (source == NULL)
        source = actor_from_payload_key(runtime, payload, json_string(action, "target_from_payload", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "source", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "actor", NULL));
    if (source == NULL)
        source = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
    return source;
}

static bool runtime_add_noise_event(slayer3d_game_data_runtime *runtime, const char *source_actor_name,
                                    slayer3d_vec3 position, float radius, float loudness, float duration)
{
    if (runtime == NULL || radius <= 0.0f || duration <= 0.0f)
        return false;
    if (runtime->noise_event_count >= runtime->noise_event_capacity)
    {
        const int new_capacity = runtime->noise_event_capacity > 0 ? runtime->noise_event_capacity * 2 : 16;
        noise_event_runtime *events =
            (noise_event_runtime *)SDL_realloc(runtime->noise_events, (size_t)new_capacity * sizeof(*events));
        if (events == NULL)
            return false;
        SDL_memset(events + runtime->noise_event_capacity, 0,
                   (size_t)(new_capacity - runtime->noise_event_capacity) * sizeof(*events));
        runtime->noise_events = events;
        runtime->noise_event_capacity = new_capacity;
    }

    noise_event_runtime *event = &runtime->noise_events[runtime->noise_event_count++];
    SDL_zero(*event);
    event->id = ++runtime->next_noise_event_id;
    if (event->id == 0U)
        event->id = ++runtime->next_noise_event_id;
    SDL_snprintf(event->key, sizeof(event->key), "noise.%u", event->id);
    event->source_actor_name = source_actor_name;
    event->position = position;
    event->radius = radius;
    event->loudness = loudness;
    event->remaining_seconds = duration;
    return true;
}

static bool execute_noise_emit_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                      const slayer3d_properties *payload)
{
    slayer3d_registered_actor *source = noise_source_actor(runtime, action, payload);
    const slayer3d_vec3 fallback = source != NULL ? source->position : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
    const slayer3d_vec3 position = actor_spawn_position_from_action(runtime, action, payload, fallback);
    const float radius = SDL_max(json_float(action, "radius", json_float(action, "range", 16.0f)), 0.0f);
    const float loudness = SDL_max(json_float(action, "loudness", 1.0f), 0.0f);
    const float duration = SDL_max(json_float(action, "duration", json_float(action, "duration_seconds", 0.1f)), 0.0f);
    return runtime_add_noise_event(runtime, source != NULL ? source->name : NULL, position, radius, loudness, duration);
}

static bool execute_one_action(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                               const slayer3d_properties *payload)
{
    const char *type = json_string(action, "type", "");
    slayer3d_signal_bus *bus = runtime_bus(runtime);
    slayer3d_timer_pool *timers = runtime_timers(runtime);

    if (SDL_strcmp(type, "signal.emit") == 0)
    {
        const int signal_id = action_signal_id(runtime, action, "signal");
        if (signal_id >= 0 && bus != NULL)
            slayer3d_signal_emit(bus, signal_id, payload);
        return signal_id >= 0;
    }

    if (SDL_strcmp(type, "timer.start") == 0)
    {
        const int timer_index = find_timer_index(runtime, json_string(action, "timer", NULL));
        if (timer_index < 0 || timers == NULL)
            return false;
        const named_timer *timer = &runtime->timers[timer_index];
        return slayer3d_timer_start(timers, json_float(action, "delay", timer->delay), timer->signal_id,
                                    timer->repeating, timer->interval) != 0;
    }

    if (SDL_strcmp(type, "property.set") == 0 || SDL_strcmp(type, "property.add") == 0)
    {
        slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        const char *value_from_payload = json_string(action, "value_from_payload", NULL);
        const slayer3d_value *payload_value = value_from_payload != NULL && payload != NULL
                                                  ? slayer3d_properties_get_value(payload, value_from_payload)
                                                  : NULL;
        if (actor == NULL || key == NULL || (value == NULL && payload_value == NULL))
            return false;

        if (SDL_strcmp(type, "property.add") == 0)
        {
            const slayer3d_value *existing = slayer3d_properties_get_value(actor->props, key);
            if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_int(actor->props, key, existing->as_int + payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_INT)
            {
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)payload_value->as_int);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && payload_value != NULL &&
                     payload_value->type == SLAYER3D_VALUE_FLOAT)
            {
                slayer3d_properties_set_float(actor->props, key, (float)existing->as_int + payload_value->as_float);
            }
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_INT && yyjson_is_num(value))
                slayer3d_properties_set_int(actor->props, key, existing->as_int + (int)yyjson_get_int(value));
            else if (existing != NULL && existing->type == SLAYER3D_VALUE_FLOAT && yyjson_is_num(value))
                slayer3d_properties_set_float(actor->props, key, existing->as_float + (float)yyjson_get_real(value));
            else
                return false;
        }
        else if (payload_value != NULL)
        {
            copy_property_value(actor->props, key, payload_value);
        }
        else
        {
            set_actor_property_from_json(actor, key, value);
        }
        return true;
    }

    if (SDL_strcmp(type, "property.snapshot") == 0)
        return snapshot_actor_properties(runtime, action);

    if (SDL_strcmp(type, "property.restore_snapshot") == 0)
        return restore_actor_property_snapshot(runtime, action);

    if (SDL_strcmp(type, "property.animate") == 0)
        return start_property_animation_from_json(runtime, action);

    if (SDL_strcmp(type, "property.reset_defaults") == 0)
        return reset_actor_properties_to_authored_defaults(runtime, action);

    if (SDL_strcmp(type, "input.reset_bindings") == 0)
        return slayer3d_game_data_reset_menu_input_bindings(runtime, json_string(action, "menu", NULL));

    if (SDL_strcmp(type, "input.apply_profile") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *profile = json_string(action, "profile", NULL);
        if (!slayer3d_game_data_apply_input_profile(runtime, input, profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "input.apply_active_profile") == 0)
    {
        char error[256] = {0};
        const char *profile = NULL;
        slayer3d_input_manager *input = runtime_input(runtime);
        if (!slayer3d_game_data_apply_active_input_profile(runtime, input, &profile, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile apply failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "game data active input profile applied: profile=%s",
                    profile != NULL ? profile : "<none>");
        return true;
    }

    if (SDL_strcmp(type, "input.clear_network_input_overrides") == 0)
    {
        char error[256] = {0};
        slayer3d_input_manager *input = runtime_input(runtime);
        const char *channel = json_string(action, "channel", NULL);
        if (!slayer3d_game_data_clear_network_input_overrides(runtime, channel, input, error, (int)sizeof(error)))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "game data network input override clear failed: %s",
                        error[0] != '\0' ? error : "unknown error");
            return false;
        }
        return true;
    }

    if (SDL_strcmp(type, "scene_state.set") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *value = obj_get(action, "value");
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || value == NULL)
            return false;
        return set_property_from_json_with_payload(runtime->scene_state, key, value, payload);
    }

    if (SDL_strcmp(type, "scene_state.toggle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
            return false;
        const bool current = scene_state_bool(runtime, key, json_bool(action, "default", false));
        slayer3d_properties_set_bool(runtime->scene_state, key, !current);
        return true;
    }

    if (SDL_strcmp(type, "scene_state.cycle") == 0)
    {
        const char *key = json_string(action, "key", NULL);
        yyjson_val *values = obj_get(action, "values");
        const size_t count = yyjson_is_arr(values) ? yyjson_arr_size(values) : 0U;
        if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0' || count == 0U)
            return false;

        const slayer3d_value *current = slayer3d_properties_get_value(runtime->scene_state, key);
        slayer3d_value fallback;
        if (current == NULL && json_scalar_to_value(obj_get(action, "default"), &fallback))
            current = &fallback;
        size_t next = 0U;
        for (size_t i = 0; i < count; ++i)
        {
            yyjson_val *value = yyjson_arr_get(values, i);
            if (json_value_matches_property(value, current))
            {
                next = (i + 1U) % count;
                break;
            }
        }

        return set_property_from_json(runtime->scene_state, key, yyjson_arr_get(values, next));
    }

    if (SDL_strcmp(type, "network.direct_connect.start") == 0)
    {
        const char *name = json_string(action, "name", NULL);
        const char *host_key = json_string(action, "host_key", NULL);
        const char *port_key = json_string(action, "port_key", NULL);
        const char *host = runtime != NULL && host_key != NULL
                               ? slayer3d_properties_get_string(runtime->scene_state, host_key,
                                                                json_string(action, "default_host", "127.0.0.1"))
                               : json_string(action, "host", json_string(action, "default_host", "127.0.0.1"));
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const char *port_text = runtime != NULL && port_key != NULL
                                    ? slayer3d_properties_get_string(runtime->scene_state, port_key, NULL)
                                    : NULL;
        const int port = port_text != NULL ? SDL_atoi(port_text) : json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_direct_connect_start(
            runtime, name, host, port, json_string(action, "status_key", NULL), json_string(action, "state_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.direct_connect.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Disconnected");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_direct_connect_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.direct_connect.observe") == 0)
    {
        return slayer3d_game_data_network_direct_connect_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.start") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        return slayer3d_game_data_network_host_start(
            runtime, json_string(action, "name", NULL), port,
            json_string(action, "session_name", json_string(action, "advertised_name", NULL)),
            json_string(action, "status_key", NULL), json_string(action, "endpoint_key", NULL),
            json_string(action, "peer_key", NULL), json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.host.cancel") == 0)
    {
        char status[256];
        const char *status_text = json_string(action, "status", "Not hosting");
        (void)format_payload_string(payload, status_text, status, sizeof(status));
        return slayer3d_game_data_network_host_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL), status);
    }

    if (SDL_strcmp(type, "network.host.observe") == 0)
    {
        return slayer3d_game_data_network_host_publish_status(
            runtime, json_string(action, "name", NULL), json_string(action, "status_key", NULL),
            json_string(action, "endpoint_key", NULL), json_string(action, "peer_key", NULL),
            json_string(action, "connected_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.start") == 0 || SDL_strcmp(type, "network.discovery.refresh") == 0)
    {
        const int default_port = json_int(action, "default_port", SLAYER3D_NETWORK_DEFAULT_PORT);
        const int port = json_int_or_string(action, "port", default_port);
        const int local_port = json_int_or_string(action, "local_port", 0);
        return slayer3d_game_data_network_discovery_start(
            runtime, json_string(action, "name", NULL), json_string(action, "host", NULL), port, local_port,
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.observe") == 0)
    {
        return slayer3d_game_data_network_discovery_update(
            runtime, json_string(action, "name", NULL),
            json_float(action, "dt", json_float(action, "update_seconds", 0.016f)),
            json_string(action, "collection", NULL), json_string(action, "status_key", NULL),
            json_string(action, "count_key", NULL));
    }

    if (SDL_strcmp(type, "network.discovery.cancel") == 0)
    {
        return slayer3d_game_data_network_discovery_cancel(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL),
            json_string(action, "status_key", NULL), json_string(action, "count_key", NULL),
            json_string(action, "status", "Discovery canceled"));
    }

    if (SDL_strcmp(type, "network.discovery.connect_selected") == 0)
    {
        const char *index_key = json_string(action, "selected_index_key", NULL);
        const int selected_index =
            runtime != NULL && runtime->scene_state != NULL && index_key != NULL
                ? slayer3d_properties_get_int(runtime->scene_state, index_key, json_int(action, "selected_index", 0))
                : json_int(action, "selected_index", 0);
        return slayer3d_game_data_network_discovery_connect_selected(
            runtime, json_string(action, "name", NULL), json_string(action, "collection", NULL), selected_index,
            json_string(action, "direct_connect_name", NULL), json_string(action, "host_key", NULL),
            json_string(action, "port_key", NULL), json_string(action, "status_key", NULL),
            json_string(action, "state_key", NULL), json_string(action, "connected_key", NULL),
            json_string(action, "connecting_status", NULL));
    }

    if (SDL_strcmp(type, "ui.animate") == 0)
        return start_ui_animation_from_json(runtime, action);

    if (SDL_strncmp(type, "audio.", 6) == 0)
        return execute_audio_action(runtime, action, payload, type);

    if (SDL_strncmp(type, "persistence.", 12) == 0)
        return execute_persistence_action(runtime, action, type);

    if (SDL_strcmp(type, "entity.set_active") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        yyjson_val *active = obj_get(action, "active");
        if (actor == NULL || !yyjson_is_bool(active))
            return false;
        actor->active = yyjson_get_bool(active);
        return true;
    }

    if (SDL_strcmp(type, "actor.spawn") == 0)
        return execute_actor_spawn_action(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn") == 0)
        return execute_actor_despawn_action_with_payload(runtime, action, payload);

    if (SDL_strcmp(type, "actor.despawn_by_tag") == 0)
        return execute_actor_despawn_by_tag_action(runtime, action);

    if (SDL_strcmp(type, "combat.damage") == 0)
        return execute_combat_damage_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.heal") == 0)
        return execute_combat_heal_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.kill") == 0)
        return execute_combat_kill_action(runtime, action, payload);
    if (SDL_strcmp(type, "combat.revive") == 0)
        return execute_combat_revive_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.add") == 0)
        return execute_resource_amount_action(runtime, action, payload, false);
    if (SDL_strcmp(type, "resource.consume") == 0)
        return execute_resource_amount_action(runtime, action, payload, true);
    if (SDL_strcmp(type, "resource.set") == 0)
        return execute_resource_set_action(runtime, action, payload);
    if (SDL_strcmp(type, "pickup.collect") == 0)
        return execute_pickup_collect_action(runtime, action, payload);
    if (SDL_strcmp(type, "resource.station.use") == 0)
        return execute_resource_station_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "status_effect.apply") == 0)
        return execute_status_effect_apply_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.reload") == 0)
        return execute_weapon_reload_action(runtime, action, payload);
    if (SDL_strcmp(type, "weapon.hitscan") == 0)
        return execute_weapon_hitscan_action(runtime, action, payload);
    if (SDL_strcmp(type, "interaction.use") == 0)
        return execute_interaction_use_action(runtime, action, payload);
    if (SDL_strcmp(type, "effect.explosion") == 0)
        return execute_effect_explosion_action(runtime, action, payload);
    if (SDL_strcmp(type, "noise.emit") == 0)
        return execute_noise_emit_action(runtime, action, payload);

    if (SDL_strcmp(type, "projectile.fire") == 0)
        return execute_projectile_fire_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.launch") == 0 || SDL_strcmp(type, "controller.fps_sector.launch") == 0)
        return execute_fps_controller_launch_action(runtime, action, payload);
    if (SDL_strcmp(type, "controller.fps.teleport") == 0 || SDL_strcmp(type, "controller.fps_sector.teleport") == 0)
        return execute_fps_controller_teleport_action(runtime, action, payload);
    if (SDL_strcmp(type, "grid.spawn_from_glyphs") == 0)
        return execute_grid_spawn_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.spawn_runs_from_glyphs") == 0)
        return execute_grid_spawn_runs_from_glyphs_action(runtime, action);
    if (SDL_strcmp(type, "grid.pickup_layer.reset") == 0)
        return execute_grid_pickup_layer_reset_action(runtime, action);

    if (SDL_strcmp(type, "sector_door.open") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "open");
    if (SDL_strcmp(type, "sector_door.close") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "close");
    if (SDL_strcmp(type, "sector_door.toggle") == 0)
        return execute_sector_door_state_action(runtime, action, payload, "toggle");
    if (SDL_strcmp(type, "sector_door.interact") == 0)
        return execute_sector_door_interact_action(runtime, action);
    if (SDL_strcmp(type, "sector_lighting.set") == 0)
    {
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        yyjson_val *color_json = obj_get(action, "color");
        if (yyjson_is_arr(color_json) && yyjson_arr_size(color_json) == 3U)
        {
            (void)json_float_array(color_json, color, 3, color);
            color[3] = 1.0f;
        }
        else
        {
            (void)json_float_array(color_json, color, 4, color);
        }
        char error[256] = {0};
        char sector_index_text[32];
        const char *sector = json_string(action, "sector", NULL);
        yyjson_val *sector_index_value = obj_get(action, "sector_index");
        if (sector == NULL && yyjson_is_int(sector_index_value))
        {
            SDL_snprintf(sector_index_text, sizeof(sector_index_text), "%d", (int)yyjson_get_int(sector_index_value));
            sector = sector_index_text;
        }
        const bool ok = slayer3d_game_data_set_sector_lighting(runtime, json_string(action, "sector_level", NULL),
                                                               sector, json_float(action, "level", 255.0f), color,
                                                               error, (int)sizeof(error));
        if (!ok)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sector_lighting.set failed: %s",
                        error[0] != '\0' ? error : "unknown error");
        return ok;
    }

    if (SDL_strcmp(type, "transform.set_position") == 0)
    {
        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, json_string(action, "target", NULL));
        if (actor == NULL)
            return false;
        actor_set_position(actor, json_vec3(action, "position", actor->position));
        return true;
    }

    if (SDL_strcmp(type, "camera.toggle") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        const char *fallback = json_string(action, "fallback", NULL);
        runtime->active_camera =
            runtime->active_camera != NULL && camera != NULL && SDL_strcmp(runtime->active_camera, camera) == 0
                ? fallback
                : camera;
        return runtime->active_camera != NULL;
    }

    if (SDL_strcmp(type, "camera.set") == 0)
    {
        const char *camera = json_string(action, "camera", NULL);
        if (camera == NULL)
            return false;
        runtime->active_camera = camera;
        return true;
    }

    if (SDL_strcmp(type, "scene.set") == 0)
    {
        yyjson_val *authored_payload = obj_get(action, "payload");
        if (authored_payload == NULL)
            return slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                    payload);

        slayer3d_properties *scene_payload = properties_from_json_payload(authored_payload, payload);
        if (scene_payload == NULL)
            return false;
        const bool ok = slayer3d_game_data_set_active_scene_with_payload(runtime, json_string(action, "scene", NULL),
                                                                         scene_payload);
        slayer3d_properties_destroy(scene_payload);
        return ok;
    }

    if (SDL_strcmp(type, "adapter.invoke") == 0)
    {
        const char *adapter_name = json_string(action, "adapter", NULL);
        adapter_entry *adapter = find_adapter(runtime, adapter_name);
        if (adapter == NULL)
            return false;
        const char *target_name = json_string(action, "target", NULL);
        if ((target_name == NULL || target_name[0] == '\0') && payload != NULL)
            target_name = slayer3d_properties_get_string(payload, "actor_name", NULL);
        slayer3d_registered_actor *target = slayer3d_game_data_find_actor(runtime, target_name);
        return invoke_adapter(runtime, adapter, target, payload);
    }

    if (SDL_strcmp(type, "branch") == 0)
    {
        yyjson_val *condition = obj_get(action, "if");
        const bool passed = eval_data_condition(runtime, condition, NULL);
        return execute_action_array(runtime, obj_get(action, passed ? "then" : "else"), payload);
    }

    return false;
}

static bool execute_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                 const slayer3d_properties *payload)
{
    if (!yyjson_is_arr(actions))
        return false;

    actor_lifecycle_defer_begin(runtime);
    bool ok = true;
    for (size_t i = 0; i < yyjson_arr_size(actions); ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        if (yyjson_is_obj(action))
            ok = execute_one_action(runtime, action, payload) && ok;
    }
    actor_lifecycle_defer_end(runtime);
    return ok;
}

static bool execute_optional_action_array(slayer3d_game_data_runtime *runtime, yyjson_val *actions,
                                          const slayer3d_properties *payload)
{
    if (actions == NULL)
        return true;
    return execute_action_array(runtime, actions, payload);
}

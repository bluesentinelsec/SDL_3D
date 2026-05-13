/**
 * @file game_data_ui_animation_runtime.c
 * @brief Runtime UI state and authored animation helpers.
 */

#include "game_data_internal.h"

void slayer3d_game_data_ui_state_init(slayer3d_game_data_ui_state *state)
{
    if (state == NULL)
        return;
    SDL_zero(*state);
    state->visible = true;
    state->scale = 1.0f;
    state->alpha = 1.0f;
    state->tint = (slayer3d_color){255, 255, 255, 255};
}

static ui_state_entry *find_ui_state_entry(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
            return &runtime->ui_states[i];
    }
    return NULL;
}

static const ui_state_entry *find_ui_state_entry_const(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
            return &runtime->ui_states[i];
    }
    return NULL;
}

static bool ensure_ui_state_capacity(slayer3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->ui_state_capacity)
        return true;

    int next_capacity = runtime->ui_state_capacity < 8 ? 8 : runtime->ui_state_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    ui_state_entry *entries =
        (ui_state_entry *)SDL_realloc(runtime->ui_states, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + runtime->ui_state_capacity, 0,
               (size_t)(next_capacity - runtime->ui_state_capacity) * sizeof(*entries));
    runtime->ui_states = entries;
    runtime->ui_state_capacity = next_capacity;
    return true;
}

static bool set_ui_state_internal(slayer3d_game_data_runtime *runtime, const char *name,
                                  const slayer3d_game_data_ui_state *state, bool animated)
{
    if (runtime == NULL || name == NULL || name[0] == '\0' || state == NULL)
        return false;

    ui_state_entry *entry = find_ui_state_entry(runtime, name);
    if (entry == NULL)
    {
        if (!ensure_ui_state_capacity(runtime, runtime->ui_state_count + 1))
            return false;
        entry = &runtime->ui_states[runtime->ui_state_count];
        entry->name = SDL_strdup(name);
        if (entry->name == NULL)
            return false;
        ++runtime->ui_state_count;
    }

    entry->state = *state;
    entry->animated = animated;
    return true;
}

bool slayer3d_game_data_set_ui_state(slayer3d_game_data_runtime *runtime, const char *name,
                                     const slayer3d_game_data_ui_state *state)
{
    return set_ui_state_internal(runtime, name, state, false);
}

bool slayer3d_game_data_get_ui_state(const slayer3d_game_data_runtime *runtime, const char *name,
                                     slayer3d_game_data_ui_state *out_state)
{
    if (out_state != NULL)
        slayer3d_game_data_ui_state_init(out_state);
    if (runtime == NULL || name == NULL || out_state == NULL)
        return false;

    const ui_state_entry *entry = find_ui_state_entry_const(runtime, name);
    if (entry == NULL)
        return false;

    *out_state = entry->state;
    return true;
}

bool slayer3d_game_data_clear_ui_state(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return false;

    for (int i = 0; i < runtime->ui_state_count; ++i)
    {
        if (runtime->ui_states[i].name != NULL && SDL_strcmp(runtime->ui_states[i].name, name) == 0)
        {
            SDL_free(runtime->ui_states[i].name);
            if (i + 1 < runtime->ui_state_count)
                runtime->ui_states[i] = runtime->ui_states[runtime->ui_state_count - 1];
            --runtime->ui_state_count;
            return true;
        }
    }
    return false;
}

void slayer3d_game_data_clear_ui_states(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->ui_state_count; ++i)
        SDL_free(runtime->ui_states[i].name);
    runtime->ui_state_count = 0;
}

static void clear_animated_ui_states(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;
    for (int i = 0; i < runtime->ui_state_count;)
    {
        if (!runtime->ui_states[i].animated)
        {
            ++i;
            continue;
        }

        SDL_free(runtime->ui_states[i].name);
        if (i + 1 < runtime->ui_state_count)
            runtime->ui_states[i] = runtime->ui_states[runtime->ui_state_count - 1];
        --runtime->ui_state_count;
    }
}

static void reset_animation_scope_if_needed(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL)
        return;

    const char *active_scene = slayer3d_game_data_active_scene(runtime);
    if (runtime->animation_scene == active_scene)
        return;

    if (runtime->animation_scene != NULL)
    {
        runtime->animation_count = 0;
        clear_animated_ui_states(runtime);
    }
    runtime->animation_scene = active_scene;
}

static bool ensure_animation_capacity(slayer3d_game_data_runtime *runtime, int required)
{
    if (runtime == NULL)
        return false;
    if (required <= runtime->animation_capacity)
        return true;

    int next_capacity = runtime->animation_capacity < 8 ? 8 : runtime->animation_capacity * 2;
    while (next_capacity < required)
        next_capacity *= 2;

    game_data_animation *entries =
        (game_data_animation *)SDL_realloc(runtime->animations, (size_t)next_capacity * sizeof(*entries));
    if (entries == NULL)
        return false;

    SDL_memset(entries + runtime->animation_capacity, 0,
               (size_t)(next_capacity - runtime->animation_capacity) * sizeof(*entries));
    runtime->animations = entries;
    runtime->animation_capacity = next_capacity;
    return true;
}

static game_data_tween_easing parse_tween_easing(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "linear") == 0)
        return GAME_DATA_TWEEN_LINEAR;
    if (SDL_strcmp(name, "in_quad") == 0)
        return GAME_DATA_TWEEN_IN_QUAD;
    if (SDL_strcmp(name, "out_quad") == 0)
        return GAME_DATA_TWEEN_OUT_QUAD;
    if (SDL_strcmp(name, "in_out_quad") == 0)
        return GAME_DATA_TWEEN_IN_OUT_QUAD;
    return GAME_DATA_TWEEN_LINEAR;
}

static game_data_tween_repeat parse_tween_repeat(const char *name)
{
    if (name == NULL || SDL_strcmp(name, "none") == 0)
        return GAME_DATA_TWEEN_REPEAT_NONE;
    if (SDL_strcmp(name, "loop") == 0)
        return GAME_DATA_TWEEN_REPEAT_LOOP;
    if (SDL_strcmp(name, "ping_pong") == 0)
        return GAME_DATA_TWEEN_REPEAT_PING_PONG;
    return GAME_DATA_TWEEN_REPEAT_NONE;
}

static float apply_tween_easing(game_data_tween_easing easing, float t)
{
    t = SDL_clamp(t, 0.0f, 1.0f);
    switch (easing)
    {
    case GAME_DATA_TWEEN_IN_QUAD:
        return t * t;
    case GAME_DATA_TWEEN_OUT_QUAD:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case GAME_DATA_TWEEN_IN_OUT_QUAD:
        return t < 0.5f ? 2.0f * t * t : 1.0f - SDL_powf(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case GAME_DATA_TWEEN_LINEAR:
    default:
        return t;
    }
}

static game_data_tween_value tween_float(float value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_FLOAT;
    out.as_float = value;
    return out;
}

static game_data_tween_value tween_vec3(slayer3d_vec3 value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_VEC3;
    out.as_vec3 = value;
    return out;
}

static game_data_tween_value tween_color(slayer3d_color value)
{
    game_data_tween_value out;
    SDL_zero(out);
    out.type = GAME_DATA_TWEEN_COLOR;
    out.as_color = value;
    return out;
}

static bool json_tween_value(yyjson_val *value, game_data_tween_value_type preferred, game_data_tween_value *out_value)
{
    if (value == NULL || out_value == NULL)
        return false;
    if (yyjson_is_num(value))
    {
        *out_value = tween_float((float)yyjson_get_num(value));
        return true;
    }
    if (yyjson_is_arr(value))
    {
        if (preferred == GAME_DATA_TWEEN_COLOR)
            *out_value = tween_color(json_color_value(value, (slayer3d_color){255, 255, 255, 255}));
        else
            *out_value = tween_vec3(json_vec3_value(value, slayer3d_vec3_make(0.0f, 0.0f, 0.0f)));
        return true;
    }
    return false;
}

static game_data_tween_value_type tween_preferred_type(const char *value_type, const slayer3d_value *current,
                                                       const char *ui_property)
{
    if (value_type != NULL && SDL_strcmp(value_type, "color") == 0)
        return GAME_DATA_TWEEN_COLOR;
    if (value_type != NULL && SDL_strcmp(value_type, "vec3") == 0)
        return GAME_DATA_TWEEN_VEC3;
    if (ui_property != NULL && (SDL_strcmp(ui_property, "tint") == 0 || SDL_strcmp(ui_property, "color") == 0))
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SLAYER3D_VALUE_COLOR)
        return GAME_DATA_TWEEN_COLOR;
    if (current != NULL && current->type == SLAYER3D_VALUE_VEC3)
        return GAME_DATA_TWEEN_VEC3;
    return GAME_DATA_TWEEN_FLOAT;
}

static bool current_property_tween_value(const slayer3d_value *current, game_data_tween_value_type preferred,
                                         game_data_tween_value *out_value, slayer3d_value_type *out_property_type)
{
    if (current == NULL || out_value == NULL || out_property_type == NULL)
        return false;

    *out_property_type = current->type;
    switch (current->type)
    {
    case SLAYER3D_VALUE_INT:
        *out_value = tween_float((float)current->as_int);
        return true;
    case SLAYER3D_VALUE_FLOAT:
        *out_value = tween_float(current->as_float);
        return true;
    case SLAYER3D_VALUE_VEC3:
        *out_value = tween_vec3(current->as_vec3);
        return true;
    case SLAYER3D_VALUE_COLOR:
        *out_value = preferred == GAME_DATA_TWEEN_COLOR
                         ? tween_color(current->as_color)
                         : tween_vec3(slayer3d_vec3_make((float)current->as_color.r, (float)current->as_color.g,
                                                         (float)current->as_color.b));
        return true;
    case SLAYER3D_VALUE_BOOL:
    case SLAYER3D_VALUE_STRING:
        return false;
    }
    return false;
}

static bool current_ui_tween_value(const slayer3d_game_data_ui_state *state, const char *property,
                                   game_data_tween_value *out_value)
{
    if (property == NULL || out_value == NULL)
        return false;
    if (SDL_strcmp(property, "alpha") == 0)
        *out_value = tween_float(state != NULL ? state->alpha : 1.0f);
    else if (SDL_strcmp(property, "scale") == 0)
        *out_value = tween_float(state != NULL ? state->scale : 1.0f);
    else if (SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "x") == 0)
        *out_value = tween_float(state != NULL ? state->offset_x : 0.0f);
    else if (SDL_strcmp(property, "offset_y") == 0 || SDL_strcmp(property, "y") == 0)
        *out_value = tween_float(state != NULL ? state->offset_y : 0.0f);
    else if (SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0)
        *out_value = tween_color(state != NULL ? state->tint : (slayer3d_color){255, 255, 255, 255});
    else
        return false;
    return true;
}

static game_data_tween_value interpolate_tween_value(const game_data_tween_value *from, const game_data_tween_value *to,
                                                     float t)
{
    if (from->type == GAME_DATA_TWEEN_VEC3 && to->type == GAME_DATA_TWEEN_VEC3)
    {
        return tween_vec3(slayer3d_vec3_make(from->as_vec3.x + (to->as_vec3.x - from->as_vec3.x) * t,
                                             from->as_vec3.y + (to->as_vec3.y - from->as_vec3.y) * t,
                                             from->as_vec3.z + (to->as_vec3.z - from->as_vec3.z) * t));
    }
    if (from->type == GAME_DATA_TWEEN_COLOR && to->type == GAME_DATA_TWEEN_COLOR)
    {
        return tween_color((slayer3d_color){
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.r + ((float)to->as_color.r - (float)from->as_color.r) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.g + ((float)to->as_color.g - (float)from->as_color.g) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.b + ((float)to->as_color.b - (float)from->as_color.b) * t + 0.5f), 0, 255),
            (Uint8)SDL_clamp(
                (int)((float)from->as_color.a + ((float)to->as_color.a - (float)from->as_color.a) * t + 0.5f), 0, 255),
        });
    }
    return tween_float(from->as_float + (to->as_float - from->as_float) * t);
}

static void apply_ui_tween_value(slayer3d_game_data_runtime *runtime, const char *target, const char *property,
                                 const game_data_tween_value *value)
{
    slayer3d_game_data_ui_state state;
    (void)slayer3d_game_data_get_ui_state(runtime, target, &state);
    if (SDL_strcmp(property, "alpha") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_ALPHA;
        state.alpha = value->as_float;
    }
    else if (SDL_strcmp(property, "scale") == 0 && value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_SCALE;
        state.scale = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_x") == 0 || SDL_strcmp(property, "x") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_x = value->as_float;
    }
    else if ((SDL_strcmp(property, "offset_y") == 0 || SDL_strcmp(property, "y") == 0) &&
             value->type == GAME_DATA_TWEEN_FLOAT)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_OFFSET;
        state.offset_y = value->as_float;
    }
    else if ((SDL_strcmp(property, "tint") == 0 || SDL_strcmp(property, "color") == 0) &&
             value->type == GAME_DATA_TWEEN_COLOR)
    {
        state.flags |= SLAYER3D_GAME_DATA_UI_STATE_TINT;
        state.tint = value->as_color;
    }
    (void)set_ui_state_internal(runtime, target, &state, true);
}

static void apply_property_tween_value(slayer3d_game_data_runtime *runtime, const game_data_animation *animation,
                                       const game_data_tween_value *value)
{
    slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, animation->target);
    if (actor == NULL || animation->key == NULL)
        return;

    if ((animation->property_type == SLAYER3D_VALUE_INT || animation->property_type == SLAYER3D_VALUE_FLOAT) &&
        value->type == GAME_DATA_TWEEN_FLOAT)
    {
        if (animation->property_type == SLAYER3D_VALUE_INT)
            slayer3d_properties_set_int(actor->props, animation->key, (int)SDL_floorf(value->as_float + 0.5f));
        else
            slayer3d_properties_set_float(actor->props, animation->key, value->as_float);
    }
    else if (animation->property_type == SLAYER3D_VALUE_VEC3 && value->type == GAME_DATA_TWEEN_VEC3)
    {
        slayer3d_properties_set_vec3(actor->props, animation->key, value->as_vec3);
    }
    else if (animation->property_type == SLAYER3D_VALUE_COLOR && value->type == GAME_DATA_TWEEN_COLOR)
    {
        slayer3d_properties_set_color(actor->props, animation->key, value->as_color);
    }
}

static void apply_animation_value(slayer3d_game_data_runtime *runtime, const game_data_animation *animation,
                                  const game_data_tween_value *value)
{
    if (animation->target_type == GAME_DATA_TWEEN_UI)
        apply_ui_tween_value(runtime, animation->target, animation->property, value);
    else
        apply_property_tween_value(runtime, animation, value);
}

static void remove_conflicting_animation(slayer3d_game_data_runtime *runtime, const game_data_animation *animation)
{
    for (int i = 0; runtime != NULL && i < runtime->animation_count;)
    {
        game_data_animation *existing = &runtime->animations[i];
        const char *existing_property =
            existing->target_type == GAME_DATA_TWEEN_PROPERTY ? existing->key : existing->property;
        const char *new_property =
            animation->target_type == GAME_DATA_TWEEN_PROPERTY ? animation->key : animation->property;
        if (existing->target_type == animation->target_type && existing->target != NULL && animation->target != NULL &&
            existing_property != NULL && new_property != NULL && SDL_strcmp(existing->target, animation->target) == 0 &&
            SDL_strcmp(existing_property, new_property) == 0)
        {
            runtime->animations[i] = runtime->animations[runtime->animation_count - 1];
            --runtime->animation_count;
            continue;
        }
        ++i;
    }
}

static bool start_animation(slayer3d_game_data_runtime *runtime, const game_data_animation *animation)
{
    if (runtime == NULL || animation == NULL || animation->target == NULL || animation->duration < 0.0f)
        return false;

    reset_animation_scope_if_needed(runtime);
    apply_animation_value(runtime, animation, &animation->from);

    if (animation->duration <= 0.0f)
    {
        apply_animation_value(runtime, animation, &animation->to);
        if (animation->done_signal_id >= 0)
            slayer3d_signal_emit(runtime_bus(runtime), animation->done_signal_id, NULL);
        return true;
    }

    remove_conflicting_animation(runtime, animation);
    if (!ensure_animation_capacity(runtime, runtime->animation_count + 1))
        return false;
    runtime->animations[runtime->animation_count] = *animation;
    ++runtime->animation_count;
    return true;
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

bool start_property_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action,
                                        const slayer3d_properties *payload)
{
    slayer3d_registered_actor *actor = action_target_actor(runtime, action, payload);
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

bool start_ui_animation_from_json(slayer3d_game_data_runtime *runtime, yyjson_val *action)
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
            const int done_signal_id = animation->done_signal_id;
            runtime->animations[i] = runtime->animations[runtime->animation_count - 1];
            --runtime->animation_count;
            if (done_signal_id >= 0 && bus != NULL)
                slayer3d_signal_emit(bus, done_signal_id, NULL);
            continue;
        }
        ++i;
    }
    return true;
}

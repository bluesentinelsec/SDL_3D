/* Runtime property effect update helpers. Included by game_data_update_runtime.inc to preserve internal linkage. */

static float move_float_toward(float value, float target, float max_delta)
{
    if (max_delta < 0.0f)
        max_delta = -max_delta;
    if (value < target)
        return SDL_min(value + max_delta, target);
    if (value > target)
        return SDL_max(value - max_delta, target);
    return target;
}

bool slayer3d_game_data_update_property_effects(slayer3d_game_data_runtime *runtime, float dt)
{
    if (runtime == NULL || runtime->doc == NULL)
        return false;
    if (dt < 0.0f)
        dt = 0.0f;

    bool ok = true;
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (!active_scene_has_entity_internal(runtime, entity_name))
            continue;

        slayer3d_registered_actor *actor = slayer3d_game_data_find_actor(runtime, entity_name);
        yyjson_val *components = obj_get(entity, "components");
        if (actor == NULL || !actor->active || !yyjson_is_arr(components))
            continue;

        for (size_t c = 0; c < yyjson_arr_size(components); ++c)
        {
            yyjson_val *component = yyjson_arr_get(components, c);
            if (SDL_strcmp(json_string(component, "type", ""), "property.decay") != 0)
                continue;

            const char *property = json_string(component, "property", NULL);
            const slayer3d_value *existing =
                property != NULL ? slayer3d_properties_get_value(actor->props, property) : NULL;
            if (property == NULL || existing == NULL ||
                (existing->type != SLAYER3D_VALUE_FLOAT && existing->type != SLAYER3D_VALUE_INT))
            {
                ok = false;
                continue;
            }

            const char *rate_property = json_string(component, "rate_property", NULL);
            const float rate = rate_property != NULL ? slayer3d_properties_get_float(actor->props, rate_property, 0.0f)
                                                     : json_float(component, "rate", 1.0f);
            const float target = json_float(component, "target", 0.0f);
            float value = existing->type == SLAYER3D_VALUE_FLOAT ? existing->as_float : (float)existing->as_int;
            value = move_float_toward(value, target, rate * dt);
            if (obj_get(component, "min") != NULL)
                value = SDL_max(value, json_float(component, "min", value));
            if (obj_get(component, "max") != NULL)
                value = SDL_min(value, json_float(component, "max", value));

            if (existing->type == SLAYER3D_VALUE_INT)
                slayer3d_properties_set_int(actor->props, property, (int)SDL_lroundf(value));
            else
                slayer3d_properties_set_float(actor->props, property, value);
        }
    }
    return ok;
}

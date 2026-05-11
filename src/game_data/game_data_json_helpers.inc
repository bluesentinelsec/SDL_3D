/* JSON helper functions shared by game-data runtime subsystems. Included by src/game_data.c to preserve internal
 * linkage. */

static yyjson_val *obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *json_string(yyjson_val *object, const char *key, const char *fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : fallback;
}

static const char *first_non_empty_string(const char *first, const char *second, const char *fallback)
{
    if (first != NULL && first[0] != '\0')
        return first;
    if (second != NULL && second[0] != '\0')
        return second;
    return fallback;
}

static char first_json_string_char(yyjson_val *object, const char *key, char fallback)
{
    const char *value = json_string(object, key, NULL);
    return value != NULL && value[0] != '\0' ? value[0] : fallback;
}

static void load_storage_config(slayer3d_game_data_runtime *runtime, yyjson_val *root);

static bool json_bool(yyjson_val *object, const char *key, bool fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

static float json_float(yyjson_val *object, const char *key, float fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_num(value) ? (float)yyjson_get_num(value) : fallback;
}

static int json_int(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = obj_get(object, key);
    return yyjson_is_int(value) ? (int)yyjson_get_int(value) : fallback;
}

static int json_int_or_string(yyjson_val *object, const char *key, int fallback)
{
    yyjson_val *value = obj_get(object, key);
    if (yyjson_is_int(value))
        return (int)yyjson_get_int(value);
    if (yyjson_is_str(value))
        return SDL_atoi(yyjson_get_str(value));
    return fallback;
}

static slayer3d_vec3 json_vec3_value(yyjson_val *value, slayer3d_vec3 fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2)
        return fallback;

    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    yyjson_val *z = yyjson_arr_get(value, 2);
    if (!yyjson_is_num(x) || !yyjson_is_num(y))
        return fallback;

    return slayer3d_vec3_make((float)yyjson_get_num(x), (float)yyjson_get_num(y),
                              yyjson_is_num(z) ? (float)yyjson_get_num(z) : fallback.z);
}

static slayer3d_vec3 json_vec3(yyjson_val *object, const char *key, slayer3d_vec3 fallback)
{
    return json_vec3_value(obj_get(object, key), fallback);
}

static slayer3d_vec4 json_vec4_value(yyjson_val *value, slayer3d_vec4 fallback)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 3)
        return fallback;

    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    yyjson_val *z = yyjson_arr_get(value, 2);
    yyjson_val *w = yyjson_arr_get(value, 3);
    if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
        return fallback;

    return slayer3d_vec4_make((float)yyjson_get_num(x), (float)yyjson_get_num(y), (float)yyjson_get_num(z),
                              yyjson_is_num(w) ? (float)yyjson_get_num(w) : fallback.w);
}

static slayer3d_vec4 json_vec4(yyjson_val *object, const char *key, slayer3d_vec4 fallback)
{
    return json_vec4_value(obj_get(object, key), fallback);
}

static bool json_vec2_value(yyjson_val *value, float fallback_x, float fallback_y, float *out_x, float *out_y)
{
    if (out_x == NULL || out_y == NULL)
        return false;
    *out_x = fallback_x;
    *out_y = fallback_y;
    if (value == NULL)
        return true;
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) < 2)
        return false;
    yyjson_val *x = yyjson_arr_get(value, 0);
    yyjson_val *y = yyjson_arr_get(value, 1);
    if (!yyjson_is_num(x) || !yyjson_is_num(y))
        return false;
    *out_x = (float)yyjson_get_num(x);
    *out_y = (float)yyjson_get_num(y);
    return true;
}

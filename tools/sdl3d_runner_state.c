/**
 * @file sdl3d_runner_state.c
 * @brief Scene-state parsing helpers for sdl3d_runner direct scene launches.
 */

#include "sdl3d_runner_state.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <limits.h>
#include <stdint.h>

#include "sdl3d/math.h"

#include "../vendor/yyjson/yyjson.h"

static void runner_state_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static bool runner_state_set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt, value != NULL ? value : "");
    return false;
}

static bool runner_state_apply_json_value(sdl3d_properties *props, const char *key, yyjson_val *value,
                                          char *error_buffer, int error_buffer_size)
{
    if (props == NULL || key == NULL || key[0] == '\0')
        return runner_state_set_errorf(error_buffer, error_buffer_size, "invalid empty state key '%s'", key);

    if (yyjson_is_str(value))
    {
        sdl3d_properties_set_string(props, key, yyjson_get_str(value));
        return true;
    }
    if (yyjson_is_bool(value))
    {
        sdl3d_properties_set_bool(props, key, yyjson_get_bool(value));
        return true;
    }
    if (yyjson_is_int(value))
    {
        const int64_t number = yyjson_get_sint(value);
        if (number < INT_MIN || number > INT_MAX)
            return runner_state_set_errorf(error_buffer, error_buffer_size, "state integer out of range for key '%s'",
                                           key);
        sdl3d_properties_set_int(props, key, (int)number);
        return true;
    }
    if (yyjson_is_real(value))
    {
        sdl3d_properties_set_float(props, key, (float)yyjson_get_real(value));
        return true;
    }
    if (yyjson_is_arr(value) && yyjson_arr_size(value) == 3u)
    {
        yyjson_val *x = yyjson_arr_get(value, 0);
        yyjson_val *y = yyjson_arr_get(value, 1);
        yyjson_val *z = yyjson_arr_get(value, 2);
        if (!yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z))
            return runner_state_set_errorf(error_buffer, error_buffer_size,
                                           "state vector must contain numbers for key '%s'", key);
        sdl3d_properties_set_vec3(
            props, key, sdl3d_vec3_make((float)yyjson_get_num(x), (float)yyjson_get_num(y), (float)yyjson_get_num(z)));
        return true;
    }
    if (yyjson_is_null(value))
    {
        sdl3d_properties_remove(props, key);
        return true;
    }

    return runner_state_set_errorf(error_buffer, error_buffer_size,
                                   "state value for key '%s' must be a scalar or three-number vector", key);
}

bool sdl3d_runner_apply_state_json_object(sdl3d_properties *props, const char *json, size_t json_size,
                                          const char *source_name, char *error_buffer, int error_buffer_size)
{
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)json, json_size, YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s JSON error %u at byte %llu: %s",
                         source_name != NULL ? source_name : "state", err.code, (unsigned long long)err.pos,
                         err.msg != NULL ? err.msg : "");
        }
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root))
    {
        yyjson_doc_free(doc);
        runner_state_set_error(error_buffer, error_buffer_size, "state JSON must be an object");
        return false;
    }

    bool ok = true;
    size_t index = 0;
    size_t max = 0;
    yyjson_val *key = NULL;
    yyjson_val *value = NULL;
    yyjson_obj_foreach(root, index, max, key, value)
    {
        if (!runner_state_apply_json_value(props, yyjson_get_str(key), value, error_buffer, error_buffer_size))
        {
            ok = false;
            break;
        }
    }
    yyjson_doc_free(doc);
    return ok;
}

bool sdl3d_runner_apply_state_assignment(sdl3d_properties *props, const char *assignment, char *error_buffer,
                                         int error_buffer_size)
{
    const char *equals = assignment != NULL ? SDL_strchr(assignment, '=') : NULL;
    if (equals == NULL || equals == assignment)
        return runner_state_set_errorf(error_buffer, error_buffer_size, "state override must use key=value: '%s'",
                                       assignment);

    const size_t key_len = (size_t)(equals - assignment);
    char key[128];
    if (key_len == 0u || key_len >= sizeof(key))
        return runner_state_set_errorf(error_buffer, error_buffer_size, "state key is too long: '%s'", assignment);
    SDL_memcpy(key, assignment, key_len);
    key[key_len] = '\0';

    const char *raw_value = equals + 1;
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)raw_value, SDL_strlen(raw_value), YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        sdl3d_properties_set_string(props, key, raw_value);
        return true;
    }

    const bool ok =
        runner_state_apply_json_value(props, key, yyjson_doc_get_root(doc), error_buffer, error_buffer_size);
    yyjson_doc_free(doc);
    return ok;
}

bool sdl3d_runner_apply_state_json_file(sdl3d_properties *props, const char *path, char *error_buffer,
                                        int error_buffer_size)
{
    size_t size = 0u;
    void *data = SDL_LoadFile(path, &size);
    if (data == NULL)
        return runner_state_set_errorf(error_buffer, error_buffer_size, "failed to read state file '%s'", path);

    const bool ok =
        sdl3d_runner_apply_state_json_object(props, (const char *)data, size, path, error_buffer, error_buffer_size);
    SDL_free(data);
    return ok;
}

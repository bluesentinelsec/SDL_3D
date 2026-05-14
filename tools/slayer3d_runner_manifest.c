/**
 * @file slayer3d_runner_manifest.c
 * @brief Editor test-run manifest helpers for slayer3d_runner.
 */

#include "slayer3d_runner_manifest.h"

#include <SDL3/SDL_stdinc.h>

#include "../vendor/yyjson/yyjson.h"

static void runner_manifest_set_error(char *error_buffer, int error_buffer_size, const char *message)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s", message != NULL ? message : "unknown error");
}

static bool runner_manifest_set_errorf(char *error_buffer, int error_buffer_size, const char *fmt, const char *value)
{
    if (error_buffer != NULL && error_buffer_size > 0)
        SDL_snprintf(error_buffer, (size_t)error_buffer_size, fmt, value != NULL ? value : "");
    return false;
}

static const char *runner_manifest_string(yyjson_val *root, const char *key)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool runner_manifest_string_is_empty(const char *value)
{
    return value == NULL || value[0] == '\0';
}

static bool runner_manifest_matches_explicit(const char *field_name, const char *explicit_value,
                                             const char *manifest_value, char *error_buffer, int error_buffer_size)
{
    if (runner_manifest_string_is_empty(explicit_value) || runner_manifest_string_is_empty(manifest_value) ||
        SDL_strcmp(explicit_value, manifest_value) == 0)
    {
        return true;
    }

    if (error_buffer != NULL && error_buffer_size > 0)
    {
        SDL_snprintf(error_buffer, (size_t)error_buffer_size,
                     "test-run manifest %s '%s' conflicts with explicit CLI value '%s'", field_name, manifest_value,
                     explicit_value);
    }
    return false;
}

static bool runner_manifest_assign_owned(const char **target, char **owned, const char *value, char *error_buffer,
                                         int error_buffer_size)
{
    if (target == NULL || owned == NULL || runner_manifest_string_is_empty(value))
        return true;

    char *copy = SDL_strdup(value);
    if (copy == NULL)
        return runner_manifest_set_errorf(error_buffer, error_buffer_size, "failed to allocate manifest value '%s'",
                                          value);
    SDL_free(*owned);
    *owned = copy;
    *target = copy;
    return true;
}

static bool runner_manifest_validate_args_array(yyjson_val *root, char *error_buffer, int error_buffer_size)
{
    yyjson_val *args = yyjson_obj_get(root, "args");
    if (args == NULL)
        return true;
    if (!yyjson_is_arr(args))
    {
        runner_manifest_set_error(error_buffer, error_buffer_size, "test-run manifest args must be an array");
        return false;
    }

    for (size_t i = 0; i < yyjson_arr_size(args); ++i)
    {
        if (!yyjson_is_str(yyjson_arr_get(args, i)))
        {
            runner_manifest_set_error(error_buffer, error_buffer_size,
                                      "test-run manifest args entries must be strings");
            return false;
        }
    }
    return true;
}

bool slayer3d_runner_apply_test_run_manifest_json(slayer3d_runner_args *args, const char *json, size_t json_size,
                                                  const char *source_name, char *error_buffer, int error_buffer_size)
{
    if (args == NULL || json == NULL)
    {
        runner_manifest_set_error(error_buffer, error_buffer_size,
                                  "test-run manifest requires runner arguments and JSON text");
        return false;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)json, json_size, YYJSON_READ_NOFLAG, NULL, &err);
    if (doc == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0)
        {
            SDL_snprintf(error_buffer, (size_t)error_buffer_size, "%s JSON error %u at byte %llu: %s",
                         source_name != NULL ? source_name : "test-run manifest", err.code, (unsigned long long)err.pos,
                         err.msg != NULL ? err.msg : "");
        }
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *schema = runner_manifest_string(root, "schema");
    const char *data = runner_manifest_string(root, "data");
    const char *scene = runner_manifest_string(root, "scene");
    const char *player_start = runner_manifest_string(root, "player_start");

    bool ok = true;
    if (!yyjson_is_obj(root))
    {
        runner_manifest_set_error(error_buffer, error_buffer_size, "test-run manifest must be a JSON object");
        ok = false;
    }
    else if (runner_manifest_string_is_empty(schema) || SDL_strcmp(schema, "slayer3d.editor_test_run.v0") != 0)
    {
        runner_manifest_set_error(error_buffer, error_buffer_size,
                                  "test-run manifest schema must be slayer3d.editor_test_run.v0");
        ok = false;
    }
    else if (runner_manifest_string_is_empty(data))
    {
        runner_manifest_set_error(error_buffer, error_buffer_size, "test-run manifest requires non-empty data");
        ok = false;
    }
    else if (runner_manifest_string_is_empty(scene) && runner_manifest_string_is_empty(player_start))
    {
        runner_manifest_set_error(error_buffer, error_buffer_size, "test-run manifest requires scene or player_start");
        ok = false;
    }
    else if (!runner_manifest_validate_args_array(root, error_buffer, error_buffer_size))
    {
        ok = false;
    }
    else if (args->data_asset_path_explicit &&
             !runner_manifest_matches_explicit("data", args->data_asset_path, data, error_buffer, error_buffer_size))
    {
        ok = false;
    }
    else if (args->scene_explicit &&
             !runner_manifest_matches_explicit("scene", args->scene, scene, error_buffer, error_buffer_size))
    {
        ok = false;
    }
    else if (args->player_start_explicit &&
             !runner_manifest_matches_explicit("player_start", args->player_start, player_start, error_buffer,
                                               error_buffer_size))
    {
        ok = false;
    }
    else
    {
        if (!args->data_asset_path_explicit)
            ok = runner_manifest_assign_owned(&args->data_asset_path, &args->owned_data_asset_path, data, error_buffer,
                                              error_buffer_size);
        if (ok && !args->scene_explicit)
            ok = runner_manifest_assign_owned(&args->scene, &args->owned_scene, scene, error_buffer, error_buffer_size);
        if (ok && !args->player_start_explicit)
        {
            ok = runner_manifest_assign_owned(&args->player_start, &args->owned_player_start, player_start,
                                              error_buffer, error_buffer_size);
        }
    }

    yyjson_doc_free(doc);
    return ok;
}

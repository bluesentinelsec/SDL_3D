/**
 * @file slayer3d_editor_cli.c
 * @brief Command-line parser for the SLAYER3D editor host.
 */

#include "slayer3d_editor_cli.h"

#include <SDL3/SDL_stdinc.h>

#include <argtable3.h>

void slayer3d_editor_args_print_usage(const char *argv0, FILE *stream)
{
    FILE *out = stream != NULL ? stream : stderr;
    const char *program = argv0 != NULL ? argv0 : "slayer3d_editor";
    fprintf(out,
            "Usage:\n"
            "  %s [--root <asset-root>] [--data <asset://editor.game.json>] [--media <media-dir>] "
            "[--scene <scene>] [--save <path>] [--test-run-output <path>] [--state <key=value> ...]\n",
            program);
}

static bool copy_string_list(const char ***out_values, int *out_count, const char **values, int count)
{
    if (out_values == NULL || out_count == NULL)
        return false;
    *out_values = NULL;
    *out_count = 0;
    if (count <= 0)
        return true;

    const char **copy = (const char **)SDL_calloc((size_t)count, sizeof(*copy));
    if (copy == NULL)
        return false;
    for (int i = 0; i < count; ++i)
        copy[i] = values[i];
    *out_values = copy;
    *out_count = count;
    return true;
}

slayer3d_tool_cli_result slayer3d_editor_args_parse(int argc, char **argv, slayer3d_editor_args *args, FILE *stream)
{
    struct arg_str *root = arg_str0(NULL, "root", "<asset-root>", "mount an editor project asset root");
    struct arg_str *data = arg_str0(NULL, "data", "<asset://editor.game.json>", "editor game-data asset path");
    struct arg_str *media = arg_str0(NULL, "media", "<media-dir>", "built-in media directory");
    struct arg_str *scene = arg_str0(NULL, "scene", "<scene>", "start directly in an editor scene");
    struct arg_str *save = arg_str0(NULL, "save", "<path>", "editable level fragment output path");
    struct arg_str *test_run_output =
        arg_str0(NULL, "test-run-output", "<path>", "runner test-run manifest output path");
    struct arg_str *state = arg_strn(NULL, "state", "<key=value>", 0, argc > 0 ? argc : 1, "scene-state override");
    struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {root, data, media, scene, save, test_run_output, state, help, end};
    const char *program = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "slayer3d_editor";
    FILE *out = stream != NULL ? stream : stderr;

    if (args == NULL || arg_nullcheck(argtable) != 0)
    {
        if (out != NULL)
            fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    SDL_zero(*args);
    const int errors = arg_parse(argc, argv, argtable);
    if (help->count > 0)
    {
        slayer3d_editor_args_print_usage(program, out);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_HELP;
    }
    if (errors > 0)
    {
        arg_print_errors(out, end, program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    args->root = root->count > 0 ? root->sval[0] : NULL;
    args->data_asset_path = data->count > 0 ? data->sval[0] : NULL;
    args->media_dir = media->count > 0 ? media->sval[0] : NULL;
    args->scene = scene->count > 0 ? scene->sval[0] : NULL;
    args->save_path = save->count > 0 ? save->sval[0] : NULL;
    args->test_run_path = test_run_output->count > 0 ? test_run_output->sval[0] : NULL;

    if ((args->root != NULL && args->root[0] == '\0') ||
        (args->data_asset_path != NULL && args->data_asset_path[0] == '\0') ||
        (args->scene != NULL && args->scene[0] == '\0') || (args->save_path != NULL && args->save_path[0] == '\0') ||
        (args->test_run_path != NULL && args->test_run_path[0] == '\0') ||
        !copy_string_list(&args->state_assignments, &args->state_assignment_count, state->sval, state->count))
    {
        fprintf(out, "%s: invalid or out-of-memory editor arguments\n", program);
        arg_freetable(argtable, SDL_arraysize(argtable));
        slayer3d_editor_args_destroy(args);
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    arg_freetable(argtable, SDL_arraysize(argtable));
    return SLAYER3D_TOOL_CLI_OK;
}

void slayer3d_editor_args_destroy(slayer3d_editor_args *args)
{
    if (args == NULL)
        return;
    SDL_free(args->state_assignments);
    args->state_assignments = NULL;
    args->state_assignment_count = 0;
}

void slayer3d_editor_args_apply_defaults(slayer3d_editor_args *args, const char *root, const char *data_asset_path,
                                         const char *save_path, const char *test_run_path)
{
    if (args == NULL)
        return;
    if ((args->root == NULL || args->root[0] == '\0') && root != NULL && root[0] != '\0')
        args->root = root;
    if ((args->data_asset_path == NULL || args->data_asset_path[0] == '\0') && data_asset_path != NULL &&
        data_asset_path[0] != '\0')
    {
        args->data_asset_path = data_asset_path;
    }
    if ((args->save_path == NULL || args->save_path[0] == '\0') && save_path != NULL && save_path[0] != '\0')
        args->save_path = save_path;
    if ((args->test_run_path == NULL || args->test_run_path[0] == '\0') && test_run_path != NULL &&
        test_run_path[0] != '\0')
    {
        args->test_run_path = test_run_path;
    }
}

static char *editor_state_assignment(const char *key, const char *value)
{
    const size_t key_len = SDL_strlen(key);
    const size_t value_len = SDL_strlen(value);
    char *assignment = (char *)SDL_malloc(key_len + 1u + value_len + 1u);
    if (assignment == NULL)
        return NULL;
    SDL_memcpy(assignment, key, key_len);
    assignment[key_len] = '=';
    SDL_memcpy(assignment + key_len + 1u, value, value_len);
    assignment[key_len + 1u + value_len] = '\0';
    return assignment;
}

static bool editor_add_arg(slayer3d_editor_runner_invocation *invocation, int *index, char *arg)
{
    if (invocation == NULL || index == NULL || *index >= invocation->argc)
        return false;
    invocation->argv[*index] = arg;
    ++(*index);
    return true;
}

bool slayer3d_editor_build_runner_invocation(const slayer3d_editor_args *args, const char *program,
                                             slayer3d_editor_runner_invocation *out_invocation)
{
    if (out_invocation == NULL)
        return false;
    SDL_zero(*out_invocation);
    if (args == NULL || args->root == NULL || args->root[0] == '\0' || args->data_asset_path == NULL ||
        args->data_asset_path[0] == '\0' || args->save_path == NULL || args->save_path[0] == '\0' ||
        args->test_run_path == NULL || args->test_run_path[0] == '\0')
    {
        return false;
    }

    int argc = 9 + args->state_assignment_count * 2;
    if (args->media_dir != NULL && args->media_dir[0] != '\0')
        argc += 2;
    if (args->scene != NULL && args->scene[0] != '\0')
        argc += 2;

    char **argv = (char **)SDL_calloc((size_t)argc + 1u, sizeof(*argv));
    if (argv == NULL)
        return false;

    out_invocation->argv = argv;
    out_invocation->argc = argc;
    int index = 0;
    char *save_assignment = editor_state_assignment("editor.save.path", args->save_path);
    char *test_run_assignment = editor_state_assignment("editor.test_run.path", args->test_run_path);
    if (save_assignment == NULL || test_run_assignment == NULL)
    {
        SDL_free(save_assignment);
        SDL_free(test_run_assignment);
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    out_invocation->owned_save_assignment = save_assignment;
    out_invocation->owned_test_run_assignment = test_run_assignment;

    if (!editor_add_arg(out_invocation, &index, (char *)(program != NULL ? program : "slayer3d_editor")) ||
        !editor_add_arg(out_invocation, &index, "--root") ||
        !editor_add_arg(out_invocation, &index, (char *)args->root) ||
        !editor_add_arg(out_invocation, &index, "--data") ||
        !editor_add_arg(out_invocation, &index, (char *)args->data_asset_path))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    if (args->media_dir != NULL && args->media_dir[0] != '\0')
    {
        if (!editor_add_arg(out_invocation, &index, "--media") ||
            !editor_add_arg(out_invocation, &index, (char *)args->media_dir))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }
    if (args->scene != NULL && args->scene[0] != '\0')
    {
        if (!editor_add_arg(out_invocation, &index, "--scene") ||
            !editor_add_arg(out_invocation, &index, (char *)args->scene))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }

    if (!editor_add_arg(out_invocation, &index, "--state") ||
        !editor_add_arg(out_invocation, &index, save_assignment) ||
        !editor_add_arg(out_invocation, &index, "--state") ||
        !editor_add_arg(out_invocation, &index, test_run_assignment))
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }

    for (int i = 0; i < args->state_assignment_count; ++i)
    {
        if (!editor_add_arg(out_invocation, &index, "--state") ||
            !editor_add_arg(out_invocation, &index, (char *)args->state_assignments[i]))
        {
            slayer3d_editor_runner_invocation_destroy(out_invocation);
            return false;
        }
    }
    if (index != argc)
    {
        slayer3d_editor_runner_invocation_destroy(out_invocation);
        return false;
    }
    return true;
}

void slayer3d_editor_runner_invocation_destroy(slayer3d_editor_runner_invocation *invocation)
{
    if (invocation == NULL)
        return;
    SDL_free(invocation->owned_save_assignment);
    SDL_free(invocation->owned_test_run_assignment);
    SDL_free(invocation->argv);
    invocation->argv = NULL;
    invocation->argc = 0;
    invocation->owned_save_assignment = NULL;
    invocation->owned_test_run_assignment = NULL;
}

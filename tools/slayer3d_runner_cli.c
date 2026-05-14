/**
 * @file slayer3d_runner_cli.c
 * @brief Command-line parser for the generic SLAYER3D runner.
 */

#include "slayer3d_runner_cli.h"

#include <SDL3/SDL_stdinc.h>

#include <argtable3.h>

void slayer3d_runner_args_print_usage(const char *argv0, FILE *stream)
{
    FILE *out = stream != NULL ? stream : stderr;
    const char *program = argv0 != NULL ? argv0 : "slayer3d_runner";
    fprintf(out,
            "Usage:\n"
            "  %s\n"
            "  %s --root <asset-root> (--data <asset://game.json> | --test-run-manifest <path-or-asset>) "
            "[--media <media-dir>] [--scene <scene>] "
            "[--player-start <name>] [--state <key=value> ...] [--state-json <object>] "
            "[--state-file <path-or-asset>]\n"
            "  %s --pack <game.slayer3dpak> (--data <asset://game.json> | --test-run-manifest <path-or-asset>) "
            "[--media <media-dir>] [--scene <scene>] "
            "[--player-start <name>] [--state <key=value> ...] [--state-json <object>] "
            "[--state-file <path-or-asset>]\n",
            program, program, program);
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
    fprintf(out,
            "  %s --embedded (--data <asset://game.json> | --test-run-manifest <path-or-asset>) "
            "[--media <media-dir>] [--scene <scene>] "
            "[--player-start <name>] [--state <key=value> ...] [--state-json <object>] "
            "[--state-file <path-or-asset>]\n",
            program);
#endif
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

void slayer3d_runner_args_destroy(slayer3d_runner_args *args)
{
    if (args == NULL)
        return;
    if (args->data_asset_path == args->owned_data_asset_path)
        args->data_asset_path = NULL;
    if (args->scene == args->owned_scene)
        args->scene = NULL;
    if (args->player_start == args->owned_player_start)
        args->player_start = NULL;
    SDL_free(args->state_assignments);
    SDL_free(args->state_json_values);
    SDL_free(args->state_files);
    SDL_free(args->owned_data_asset_path);
    SDL_free(args->owned_scene);
    SDL_free(args->owned_player_start);
    args->state_assignments = NULL;
    args->state_json_values = NULL;
    args->state_files = NULL;
    args->owned_data_asset_path = NULL;
    args->owned_scene = NULL;
    args->owned_player_start = NULL;
    args->state_assignment_count = 0;
    args->state_json_count = 0;
    args->state_file_count = 0;
}

slayer3d_tool_cli_result slayer3d_runner_args_parse(int argc, char **argv, slayer3d_runner_args *args, FILE *stream)
{
    struct arg_str *root = arg_str0(NULL, "root", "<asset-root>", "mount a directory asset root");
    struct arg_str *pack = arg_str0(NULL, "pack", "<game.slayer3dpak>", "mount an SLAYER3D asset pack");
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
    struct arg_lit *embedded = arg_lit0(NULL, "embedded", "mount the runner's embedded asset pack");
#endif
    struct arg_str *data = arg_str0(NULL, "data", "<asset://game.json>", "root game-data asset path");
    struct arg_str *test_run_manifest =
        arg_str0(NULL, "test-run-manifest", "<path-or-asset>", "editor test-run handoff manifest");
    struct arg_str *media = arg_str0(NULL, "media", "<media-dir>", "built-in media directory");
    struct arg_str *scene = arg_str0(NULL, "scene", "<scene>", "start directly in an authored scene");
    struct arg_str *player_start =
        arg_str0(NULL, "player-start", "<name>", "apply an editor player start before first scene enter");
    struct arg_str *state = arg_strn(NULL, "state", "<key=value>", 0, argc > 0 ? argc : 1, "scene-state override");
    struct arg_str *state_json =
        arg_strn(NULL, "state-json", "<json-object>", 0, argc > 0 ? argc : 1, "scene-state JSON object");
    struct arg_str *state_file =
        arg_strn(NULL, "state-file", "<path-or-asset>", 0, argc > 0 ? argc : 1, "scene-state JSON file");
    struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {
        root,         pack,
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
        embedded,
#endif
        data,         test_run_manifest,
        media,        scene,
        player_start, state,
        state_json,   state_file,
        help,         end,
    };
    const char *program = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "slayer3d_runner";
    FILE *out = stream != NULL ? stream : stderr;

    if (args == NULL || arg_nullcheck(argtable) != 0)
    {
        if (out != NULL)
            fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    SDL_zero(*args);
#if defined(SLAYER3D_MEDIA_DIR)
    args->media_dir = SLAYER3D_MEDIA_DIR;
#endif
#if defined(SLAYER3D_RUNNER_DEFAULT_DATA_ASSET_PATH)
    args->data_asset_path = SLAYER3D_RUNNER_DEFAULT_DATA_ASSET_PATH;
#endif
#if defined(SLAYER3D_RUNNER_DEFAULT_EMBEDDED) && defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
    args->mount_kind = SLAYER3D_RUNNER_MOUNT_EMBEDDED;
#endif

    const int errors = arg_parse(argc, argv, argtable);
    if (help->count > 0)
    {
        slayer3d_runner_args_print_usage(program, out);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_HELP;
    }

    if (errors > 0)
    {
        arg_print_errors(out, end, program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    const int explicit_mount_count = root->count + pack->count
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
                                     + embedded->count
#endif
        ;
    if (explicit_mount_count > 1)
    {
        fprintf(out, "%s: specify only one of --root, --pack, or --embedded\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    if (root->count > 0)
    {
        args->mount_kind = SLAYER3D_RUNNER_MOUNT_DIRECTORY;
        args->mount_path = root->sval[0];
    }
    else if (pack->count > 0)
    {
        args->mount_kind = SLAYER3D_RUNNER_MOUNT_PACK;
        args->mount_path = pack->sval[0];
    }
#if defined(SLAYER3D_RUNNER_EMBEDDED_ASSETS)
    else if (embedded->count > 0)
    {
        args->mount_kind = SLAYER3D_RUNNER_MOUNT_EMBEDDED;
        args->mount_path = NULL;
    }
#endif

    if (data->count > 0)
    {
        args->data_asset_path = data->sval[0];
        args->data_asset_path_explicit = true;
    }
    if (test_run_manifest->count > 0)
        args->test_run_manifest_path = test_run_manifest->sval[0];
    if (media->count > 0)
        args->media_dir = media->sval[0];
    if (scene->count > 0)
    {
        args->scene = scene->sval[0];
        args->scene_explicit = true;
    }
    if (player_start->count > 0)
    {
        args->player_start = player_start->sval[0];
        args->player_start_explicit = true;
    }

    const bool mount_path_required =
        args->mount_kind == SLAYER3D_RUNNER_MOUNT_DIRECTORY || args->mount_kind == SLAYER3D_RUNNER_MOUNT_PACK;
    if ((mount_path_required && (args->mount_path == NULL || args->mount_path[0] == '\0')) ||
        (args->mount_kind != SLAYER3D_RUNNER_MOUNT_NONE &&
         (args->data_asset_path == NULL || args->data_asset_path[0] == '\0') &&
         (args->test_run_manifest_path == NULL || args->test_run_manifest_path[0] == '\0')))
    {
        fprintf(out, "%s: explicit asset mounts require --data or --test-run-manifest\n", program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    if ((args->scene != NULL && args->scene[0] == '\0') ||
        (args->player_start != NULL && args->player_start[0] == '\0') ||
        (args->test_run_manifest_path != NULL && args->test_run_manifest_path[0] == '\0') ||
        !copy_string_list(&args->state_assignments, &args->state_assignment_count, state->sval, state->count) ||
        !copy_string_list(&args->state_json_values, &args->state_json_count, state_json->sval, state_json->count) ||
        !copy_string_list(&args->state_files, &args->state_file_count, state_file->sval, state_file->count))
    {
        fprintf(out, "%s: invalid or out-of-memory direct-start arguments\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        slayer3d_runner_args_destroy(args);
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return SLAYER3D_TOOL_CLI_OK;
}

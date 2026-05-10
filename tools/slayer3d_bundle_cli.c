/**
 * @file slayer3d_bundle_cli.c
 * @brief Command-line parser for SLAYER3D fused executable bundling.
 */

#include "slayer3d_bundle_cli.h"

#include <SDL3/SDL_stdinc.h>

#include <argtable3.h>

void slayer3d_bundle_args_print_usage(const char *argv0, FILE *stream)
{
    FILE *out = stream != NULL ? stream : stderr;
    fprintf(out,
            "Usage: %s --runner <slayer3d_runner> --root <asset-root> --data <asset://game.json> --output <game> "
            "[--file <relative-path> ...]\n",
            argv0 != NULL ? argv0 : "slayer3d_bundle");
}

void slayer3d_bundle_args_destroy(slayer3d_bundle_args *args)
{
    if (args == NULL)
        return;
    SDL_free(args->files);
    args->files = NULL;
    args->file_count = 0;
}

slayer3d_tool_cli_result slayer3d_bundle_args_parse(int argc, char **argv, slayer3d_bundle_args *args, FILE *stream)
{
    struct arg_str *runner = arg_str0(NULL, "runner", "<slayer3d_runner>", "generic runner executable to copy");
    struct arg_str *root = arg_str0(NULL, "root", "<asset-root>", "asset root directory");
    struct arg_str *data = arg_str0(NULL, "data", "<asset://game.json>", "default game-data asset path");
    struct arg_str *output = arg_str0(NULL, "output", "<game>", "output fused executable");
    struct arg_str *files = arg_strn(NULL, "file", "<relative-path>", 0, argc > 0 ? argc : 1,
                                     "asset file to include; omit to include the whole root recursively");
    struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {runner, root, data, output, files, help, end};
    const char *program = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "slayer3d_bundle";
    FILE *out = stream != NULL ? stream : stderr;

    if (args == NULL || arg_nullcheck(argtable) != 0)
    {
        if (out != NULL)
            fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    SDL_zero(*args);
    const int errors = arg_parse(argc, argv, argtable);
    if (help->count > 0)
    {
        slayer3d_bundle_args_print_usage(program, out);
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

    if (runner->count > 0)
        args->runner = runner->sval[0];
    if (root->count > 0)
        args->root = root->sval[0];
    if (data->count > 0)
        args->data_asset_path = data->sval[0];
    if (output->count > 0)
        args->output = output->sval[0];
    args->file_count = files->count;

    if (args->runner == NULL || args->runner[0] == '\0' || args->root == NULL || args->root[0] == '\0' ||
        args->data_asset_path == NULL || args->data_asset_path[0] == '\0' || args->output == NULL ||
        args->output[0] == '\0')
    {
        fprintf(out, "%s: --runner, --root, --data, and --output are required\n", program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        SDL_zero(*args);
        return SLAYER3D_TOOL_CLI_ERROR;
    }
    if (SDL_strncmp(args->data_asset_path, "asset://", 8) != 0)
    {
        fprintf(out, "%s: --data must be an asset:// path\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        SDL_zero(*args);
        return SLAYER3D_TOOL_CLI_ERROR;
    }
    if (SDL_strcmp(args->runner, args->output) == 0)
    {
        fprintf(out, "%s: --output must not overwrite --runner\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        SDL_zero(*args);
        return SLAYER3D_TOOL_CLI_ERROR;
    }

    if (args->file_count > 0)
    {
        args->files = (const char **)SDL_calloc((size_t)args->file_count, sizeof(*args->files));
        if (args->files == NULL)
        {
            fprintf(out, "%s: insufficient memory\n", program);
            arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
            SDL_zero(*args);
            return SLAYER3D_TOOL_CLI_ERROR;
        }
        for (int i = 0; i < args->file_count; ++i)
        {
            if (files->sval[i] == NULL || files->sval[i][0] == '\0')
            {
                fprintf(out, "%s: --file values must be non-empty\n", program);
                arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
                slayer3d_bundle_args_destroy(args);
                return SLAYER3D_TOOL_CLI_ERROR;
            }
            args->files[i] = files->sval[i];
        }
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return SLAYER3D_TOOL_CLI_OK;
}

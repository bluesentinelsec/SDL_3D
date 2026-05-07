/**
 * @file sdl3d_pack_cli.c
 * @brief Command-line parser for the SDL3D asset pack tool.
 */

#include "sdl3d_pack_cli.h"

#include <SDL3/SDL_stdinc.h>

#include <argtable3.h>

void sdl3d_pack_args_print_usage(const char *argv0, FILE *stream)
{
    FILE *out = stream != NULL ? stream : stderr;
    fprintf(out, "Usage: %s --output <pack.sdl3dpak> --root <asset-root> --file <relative-path> [--file ...]\n",
            argv0 != NULL ? argv0 : "sdl3d_pack");
}

void sdl3d_pack_args_destroy(sdl3d_pack_args *args)
{
    if (args == NULL)
        return;
    SDL_free(args->files);
    args->files = NULL;
    args->file_count = 0;
}

sdl3d_tool_cli_result sdl3d_pack_args_parse(int argc, char **argv, sdl3d_pack_args *args, FILE *stream)
{
    struct arg_str *output = arg_str0(NULL, "output", "<pack.sdl3dpak>", "output pack file");
    struct arg_str *root = arg_str0(NULL, "root", "<asset-root>", "asset root directory");
    struct arg_str *files = arg_strn(NULL, "file", "<relative-path>", 0, argc > 0 ? argc : 1, "asset file to include");
    struct arg_lit *help = arg_lit0("h", "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {output, root, files, help, end};
    const char *program = argc > 0 && argv != NULL && argv[0] != NULL ? argv[0] : "sdl3d_pack";
    FILE *out = stream != NULL ? stream : stderr;

    if (args == NULL || arg_nullcheck(argtable) != 0)
    {
        if (out != NULL)
            fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SDL3D_TOOL_CLI_ERROR;
    }

    SDL_zero(*args);
    const int errors = arg_parse(argc, argv, argtable);
    if (help->count > 0)
    {
        sdl3d_pack_args_print_usage(program, out);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SDL3D_TOOL_CLI_HELP;
    }

    if (errors > 0)
    {
        arg_print_errors(out, end, program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return SDL3D_TOOL_CLI_ERROR;
    }

    if (output->count > 0)
        args->output = output->sval[0];
    if (root->count > 0)
        args->root = root->sval[0];
    args->file_count = files->count;

    if (args->output == NULL || args->output[0] == '\0' || args->root == NULL || args->root[0] == '\0' ||
        args->file_count <= 0)
    {
        fprintf(out, "%s: --output, --root, and at least one --file are required\n", program);
        fprintf(out, "Try '%s --help' for more information.\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        SDL_zero(*args);
        return SDL3D_TOOL_CLI_ERROR;
    }

    args->files = (const char **)SDL_calloc((size_t)args->file_count, sizeof(*args->files));
    if (args->files == NULL)
    {
        fprintf(out, "%s: insufficient memory\n", program);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        SDL_zero(*args);
        return SDL3D_TOOL_CLI_ERROR;
    }
    for (int i = 0; i < args->file_count; ++i)
    {
        if (files->sval[i] == NULL || files->sval[i][0] == '\0')
        {
            fprintf(out, "%s: --file values must be non-empty\n", program);
            arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
            sdl3d_pack_args_destroy(args);
            return SDL3D_TOOL_CLI_ERROR;
        }
        args->files[i] = files->sval[i];
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return SDL3D_TOOL_CLI_OK;
}

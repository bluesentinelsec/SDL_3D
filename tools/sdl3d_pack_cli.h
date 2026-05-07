/**
 * @file sdl3d_pack_cli.h
 * @brief Command-line parser for the SDL3D asset pack tool.
 */

#ifndef SDL3D_PACK_CLI_H
#define SDL3D_PACK_CLI_H

#include <stdio.h>

#include "sdl3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_pack_args
    {
        const char *output;
        const char *root;
        const char **files;
        int file_count;
    } sdl3d_pack_args;

    sdl3d_tool_cli_result sdl3d_pack_args_parse(int argc, char **argv, sdl3d_pack_args *args, FILE *stream);
    void sdl3d_pack_args_destroy(sdl3d_pack_args *args);
    void sdl3d_pack_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

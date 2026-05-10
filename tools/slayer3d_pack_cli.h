/**
 * @file slayer3d_pack_cli.h
 * @brief Command-line parser for the SLAYER3D asset pack tool.
 */

#ifndef SLAYER3D_PACK_CLI_H
#define SLAYER3D_PACK_CLI_H

#include <stdio.h>

#include "slayer3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_pack_args
    {
        const char *output;
        const char *root;
        const char **files;
        int file_count;
    } slayer3d_pack_args;

    slayer3d_tool_cli_result slayer3d_pack_args_parse(int argc, char **argv, slayer3d_pack_args *args, FILE *stream);
    void slayer3d_pack_args_destroy(slayer3d_pack_args *args);
    void slayer3d_pack_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

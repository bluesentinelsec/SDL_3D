/**
 * @file sdl3d_bundle_cli.h
 * @brief Command-line parser for SDL3D fused executable bundling.
 */

#ifndef SDL3D_BUNDLE_CLI_H
#define SDL3D_BUNDLE_CLI_H

#include <stdio.h>

#include "sdl3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sdl3d_bundle_args
    {
        const char *runner;
        const char *root;
        const char *data_asset_path;
        const char *output;
        const char **files;
        int file_count;
    } sdl3d_bundle_args;

    sdl3d_tool_cli_result sdl3d_bundle_args_parse(int argc, char **argv, sdl3d_bundle_args *args, FILE *stream);
    void sdl3d_bundle_args_destroy(sdl3d_bundle_args *args);
    void sdl3d_bundle_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_BUNDLE_CLI_H */

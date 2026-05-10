/**
 * @file slayer3d_bundle_cli.h
 * @brief Command-line parser for SLAYER3D fused executable bundling.
 */

#ifndef SLAYER3D_BUNDLE_CLI_H
#define SLAYER3D_BUNDLE_CLI_H

#include <stdio.h>

#include "slayer3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_bundle_args
    {
        const char *runner;
        const char *root;
        const char *data_asset_path;
        const char *output;
        const char **files;
        int file_count;
    } slayer3d_bundle_args;

    slayer3d_tool_cli_result slayer3d_bundle_args_parse(int argc, char **argv, slayer3d_bundle_args *args,
                                                        FILE *stream);
    void slayer3d_bundle_args_destroy(slayer3d_bundle_args *args);
    void slayer3d_bundle_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_BUNDLE_CLI_H */

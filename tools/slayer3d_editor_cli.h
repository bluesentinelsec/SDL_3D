/**
 * @file slayer3d_editor_cli.h
 * @brief Command-line parser for the SLAYER3D editor host.
 */

#ifndef SLAYER3D_EDITOR_CLI_H
#define SLAYER3D_EDITOR_CLI_H

#include <stdbool.h>
#include <stdio.h>

#include "slayer3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct slayer3d_editor_args
    {
        const char *root;
        const char *data_asset_path;
        const char *media_dir;
        const char *scene;
        const char *save_path;
        const char *test_run_path;
        const char **state_assignments;
        int state_assignment_count;
    } slayer3d_editor_args;

    typedef struct slayer3d_editor_runner_invocation
    {
        char **argv;
        int argc;
        char *owned_save_assignment;
        char *owned_test_run_assignment;
    } slayer3d_editor_runner_invocation;

    slayer3d_tool_cli_result slayer3d_editor_args_parse(int argc, char **argv, slayer3d_editor_args *args,
                                                        FILE *stream);
    void slayer3d_editor_args_destroy(slayer3d_editor_args *args);
    void slayer3d_editor_args_apply_defaults(slayer3d_editor_args *args, const char *root, const char *data_asset_path,
                                             const char *save_path, const char *test_run_path);
    bool slayer3d_editor_build_runner_invocation(const slayer3d_editor_args *args, const char *program,
                                                 slayer3d_editor_runner_invocation *out_invocation);
    void slayer3d_editor_runner_invocation_destroy(slayer3d_editor_runner_invocation *invocation);
    void slayer3d_editor_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

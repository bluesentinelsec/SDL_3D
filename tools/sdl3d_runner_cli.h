/**
 * @file sdl3d_runner_cli.h
 * @brief Command-line parser for the generic SDL3D runner.
 */

#ifndef SDL3D_RUNNER_CLI_H
#define SDL3D_RUNNER_CLI_H

#include <stdbool.h>
#include <stdio.h>

#include "sdl3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_runner_mount_kind
    {
        SDL3D_RUNNER_MOUNT_NONE = 0,
        SDL3D_RUNNER_MOUNT_DIRECTORY,
        SDL3D_RUNNER_MOUNT_PACK,
        SDL3D_RUNNER_MOUNT_EMBEDDED
    } sdl3d_runner_mount_kind;

    typedef struct sdl3d_runner_args
    {
        sdl3d_runner_mount_kind mount_kind;
        const char *mount_path;
        const char *data_asset_path;
        const char *media_dir;
        const char *scene;
        const char **state_assignments;
        int state_assignment_count;
        const char **state_json_values;
        int state_json_count;
        const char **state_files;
        int state_file_count;
    } sdl3d_runner_args;

    sdl3d_tool_cli_result sdl3d_runner_args_parse(int argc, char **argv, sdl3d_runner_args *args, FILE *stream);
    void sdl3d_runner_args_destroy(sdl3d_runner_args *args);
    void sdl3d_runner_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

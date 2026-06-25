/**
 * @file slayer3d_runner_cli.h
 * @brief Command-line parser for the generic SLAYER3D runner.
 */

#ifndef SLAYER3D_RUNNER_CLI_H
#define SLAYER3D_RUNNER_CLI_H

#include <stdbool.h>
#include <stdio.h>

#include "slayer3d_tool_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_runner_mount_kind
    {
        SLAYER3D_RUNNER_MOUNT_NONE = 0,
        SLAYER3D_RUNNER_MOUNT_DIRECTORY,
        SLAYER3D_RUNNER_MOUNT_PACK,
        SLAYER3D_RUNNER_MOUNT_EMBEDDED
    } slayer3d_runner_mount_kind;

    typedef struct slayer3d_runner_args
    {
        slayer3d_runner_mount_kind mount_kind;
        const char *mount_path;
        const char *data_asset_path;
        const char *map_path;
        const char *test_run_manifest_path;
        const char *media_dir;
        const char *scene;
        const char *player_start;
        bool data_asset_path_explicit;
        bool scene_explicit;
        bool player_start_explicit;
        char *owned_data_asset_path;
        char *owned_scene;
        char *owned_player_start;
        const char **state_assignments;
        int state_assignment_count;
        const char **state_json_values;
        int state_json_count;
        const char **state_files;
        int state_file_count;
    } slayer3d_runner_args;

    slayer3d_tool_cli_result slayer3d_runner_args_parse(int argc, char **argv, slayer3d_runner_args *args,
                                                        FILE *stream);
    int slayer3d_runner_main(int argc, char **argv);
    void slayer3d_runner_args_destroy(slayer3d_runner_args *args);
    void slayer3d_runner_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

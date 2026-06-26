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

    typedef enum slayer3d_editor_command
    {
        SLAYER3D_EDITOR_COMMAND_NONE = 0,
        SLAYER3D_EDITOR_COMMAND_NEW,
        SLAYER3D_EDITOR_COMMAND_OPEN,
        SLAYER3D_EDITOR_COMMAND_CHECK,
        SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN
    } slayer3d_editor_command;

    typedef struct slayer3d_editor_args
    {
        slayer3d_editor_command command;
        const char *project;
        const char *input_path;
        const char *output_path;
        const char *texture_path;
        const char *model_path;
        char *owned_project;
        char *owned_output_path;
        bool overwrite;
        bool lighting_preview_quality;
        bool lighting_final_quality;
        bool lighting_no_dynamic_preview;
        bool lighting_manifest;
        bool lighting_static_artifact;
        int max_dynamic_lights;
        int max_static_lights;
    } slayer3d_editor_args;

    typedef struct slayer3d_editor_asset_source
    {
        char *path;
        char *relative_path;
        bool available;
    } slayer3d_editor_asset_source;

    typedef struct slayer3d_editor_asset_sources
    {
        slayer3d_editor_asset_source textures;
        slayer3d_editor_asset_source models;
        slayer3d_editor_asset_source sprites;
        slayer3d_editor_asset_source skyboxes;
        slayer3d_editor_asset_source effects;
    } slayer3d_editor_asset_sources;

    typedef struct slayer3d_editor_project
    {
        char *project_dir;
        char *data_root;
        char *data_root_relative_path;
        char *editor_entry;
        const char *media_dir;
        char *owned_media_dir;
        char *test_run_path;
        slayer3d_editor_asset_sources asset_sources;
    } slayer3d_editor_project;

    typedef struct slayer3d_editor_launch
    {
        const char *root;
        const char *data_asset_path;
        const char *media_dir;
        const char *input_path;
        const char *save_path;
        const char *test_run_path;
        const char *project_dir;
        const char *data_root_relative_path;
        const slayer3d_editor_asset_sources *asset_sources;
        slayer3d_editor_asset_sources owned_asset_sources;
        bool owns_asset_sources;
    } slayer3d_editor_launch;

    typedef struct slayer3d_editor_runner_invocation
    {
        char **argv;
        int argc;
        char *owned_command_assignment;
        char *owned_input_assignment;
        char *owned_save_assignment;
        char *owned_test_run_assignment;
        char *owned_project_dir_assignment;
        char *owned_project_data_root_assignment;
        char *owned_asset_source_assignments[16];
    } slayer3d_editor_runner_invocation;

    slayer3d_tool_cli_result slayer3d_editor_args_parse(int argc, char **argv, slayer3d_editor_args *args,
                                                        FILE *stream);
    void slayer3d_editor_args_destroy(slayer3d_editor_args *args);
    bool slayer3d_editor_project_load(const char *project_arg, slayer3d_editor_project *out_project, char *error_buffer,
                                      int error_buffer_size);
    void slayer3d_editor_project_destroy(slayer3d_editor_project *project);
    bool slayer3d_editor_prepare_launch(const slayer3d_editor_args *args, const slayer3d_editor_project *project,
                                        slayer3d_editor_launch *out_launch, char *error_buffer, int error_buffer_size);
    void slayer3d_editor_launch_destroy(slayer3d_editor_launch *launch);
    bool slayer3d_editor_validate_paths(const slayer3d_editor_args *args, const slayer3d_editor_launch *launch,
                                        char *error_buffer, int error_buffer_size);
    bool slayer3d_editor_build_runner_invocation(const slayer3d_editor_launch *launch, const char *program,
                                                 slayer3d_editor_runner_invocation *out_invocation);
    void slayer3d_editor_runner_invocation_destroy(slayer3d_editor_runner_invocation *invocation);
    bool slayer3d_editor_run_lighting_plan(const slayer3d_editor_args *args, FILE *stream, char *error_buffer,
                                           int error_buffer_size);
    void slayer3d_editor_args_print_usage(const char *argv0, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif

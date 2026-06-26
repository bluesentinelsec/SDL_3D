/**
 * @file slayer3d_editor.c
 * @brief Generic SLAYER3D data-authored editor host.
 */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>

#include "slayer3d_editor_cli.h"
#include "slayer3d_runner_cli.h"

int main(int argc, char **argv)
{
    slayer3d_editor_args args;
    const slayer3d_tool_cli_result cli_result = slayer3d_editor_args_parse(argc, argv, &args, stderr);
    if (cli_result != SLAYER3D_TOOL_CLI_OK)
        return cli_result == SLAYER3D_TOOL_CLI_HELP ? 0 : 2;

    char error[512];
    error[0] = '\0';
    if (args.command == SLAYER3D_EDITOR_COMMAND_LIGHTING_PLAN)
    {
        const bool ok = slayer3d_editor_run_lighting_plan(&args, stdout, error, (int)sizeof(error));
        if (!ok)
            fprintf(stderr, "slayer3d_editor: %s\n", error[0] != '\0' ? error : "failed to build lighting plan");
        slayer3d_editor_args_destroy(&args);
        return ok ? 0 : 2;
    }

    slayer3d_editor_project project;
    if (!slayer3d_editor_project_load(args.project, &project, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_editor: %s\n", error[0] != '\0' ? error : "failed to load project manifest");
        slayer3d_editor_args_destroy(&args);
        return 2;
    }

    slayer3d_editor_launch launch;
    if (!slayer3d_editor_prepare_launch(&args, &project, &launch, error, (int)sizeof(error)) ||
        !slayer3d_editor_validate_paths(&args, &launch, error, (int)sizeof(error)))
    {
        fprintf(stderr, "slayer3d_editor: %s\n", error[0] != '\0' ? error : "invalid editor launch configuration");
        slayer3d_editor_project_destroy(&project);
        slayer3d_editor_args_destroy(&args);
        return 2;
    }

    slayer3d_editor_runner_invocation invocation;
    if (!slayer3d_editor_build_runner_invocation(&launch, argc > 0 && argv != NULL ? argv[0] : "slayer3d_editor",
                                                 &invocation))
    {
        fprintf(stderr, "slayer3d_editor: failed to build runner invocation\n");
        slayer3d_editor_launch_destroy(&launch);
        slayer3d_editor_project_destroy(&project);
        slayer3d_editor_args_destroy(&args);
        return 2;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor project: %s", project.project_dir);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor save path: %s", launch.save_path);
    if (launch.input_path != NULL && launch.input_path[0] != '\0')
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor input path: %s", launch.input_path);
    const int result = slayer3d_runner_main(invocation.argc, invocation.argv);
    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_launch_destroy(&launch);
    slayer3d_editor_project_destroy(&project);
    slayer3d_editor_args_destroy(&args);
    return result;
}

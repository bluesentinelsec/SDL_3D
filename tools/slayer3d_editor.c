/**
 * @file slayer3d_editor.c
 * @brief Generic SLAYER3D data-authored editor host.
 */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>

#include "slayer3d_editor_cli.h"
#include "slayer3d_runner_cli.h"

#if !defined(SLAYER3D_EDITOR_DEFAULT_ROOT)
#define SLAYER3D_EDITOR_DEFAULT_ROOT ""
#endif

#if !defined(SLAYER3D_EDITOR_DEFAULT_DATA)
#define SLAYER3D_EDITOR_DEFAULT_DATA ""
#endif

#if !defined(SLAYER3D_EDITOR_DEFAULT_SAVE_PATH)
#define SLAYER3D_EDITOR_DEFAULT_SAVE_PATH ""
#endif

#if !defined(SLAYER3D_EDITOR_DEFAULT_TEST_RUN_PATH)
#define SLAYER3D_EDITOR_DEFAULT_TEST_RUN_PATH ""
#endif

int main(int argc, char **argv)
{
    slayer3d_editor_args args;
    const slayer3d_tool_cli_result cli_result = slayer3d_editor_args_parse(argc, argv, &args, stderr);
    if (cli_result != SLAYER3D_TOOL_CLI_OK)
        return cli_result == SLAYER3D_TOOL_CLI_HELP ? 0 : 2;

    slayer3d_editor_args_apply_defaults(&args, SLAYER3D_EDITOR_DEFAULT_ROOT, SLAYER3D_EDITOR_DEFAULT_DATA,
                                        SLAYER3D_EDITOR_DEFAULT_SAVE_PATH, SLAYER3D_EDITOR_DEFAULT_TEST_RUN_PATH);
    slayer3d_editor_runner_invocation invocation;
    if (!slayer3d_editor_build_runner_invocation(&args, argc > 0 && argv != NULL ? argv[0] : "slayer3d_editor",
                                                 &invocation))
    {
        fprintf(stderr,
                "slayer3d_editor: --root, --data, --save, and --test-run-output are required unless build defaults "
                "provide them\n");
        slayer3d_editor_args_destroy(&args);
        return 2;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor save path: %s", args.save_path);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLAYER3D editor test-run manifest: %s", args.test_run_path);
    const int result = slayer3d_runner_main(invocation.argc, invocation.argv);
    slayer3d_editor_runner_invocation_destroy(&invocation);
    slayer3d_editor_args_destroy(&args);
    return result;
}

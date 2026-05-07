/**
 * @file sdl3d_tool_cli.h
 * @brief Shared command-line parsing result codes for SDL3D tools.
 */

#ifndef SDL3D_TOOL_CLI_H
#define SDL3D_TOOL_CLI_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum sdl3d_tool_cli_result
    {
        SDL3D_TOOL_CLI_OK = 0,
        SDL3D_TOOL_CLI_HELP,
        SDL3D_TOOL_CLI_ERROR
    } sdl3d_tool_cli_result;

#ifdef __cplusplus
}
#endif

#endif

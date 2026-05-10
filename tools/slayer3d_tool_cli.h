/**
 * @file slayer3d_tool_cli.h
 * @brief Shared command-line parsing result codes for SLAYER3D tools.
 */

#ifndef SLAYER3D_TOOL_CLI_H
#define SLAYER3D_TOOL_CLI_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum slayer3d_tool_cli_result
    {
        SLAYER3D_TOOL_CLI_OK = 0,
        SLAYER3D_TOOL_CLI_HELP,
        SLAYER3D_TOOL_CLI_ERROR
    } slayer3d_tool_cli_result;

#ifdef __cplusplus
}
#endif

#endif

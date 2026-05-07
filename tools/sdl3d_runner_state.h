/**
 * @file sdl3d_runner_state.h
 * @brief Scene-state parsing helpers for sdl3d_runner direct scene launches.
 */

#ifndef SDL3D_RUNNER_STATE_H
#define SDL3D_RUNNER_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "sdl3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool sdl3d_runner_apply_state_assignment(sdl3d_properties *props, const char *assignment, char *error_buffer,
                                             int error_buffer_size);
    bool sdl3d_runner_apply_state_json_object(sdl3d_properties *props, const char *json, size_t json_size,
                                              const char *source_name, char *error_buffer, int error_buffer_size);
    bool sdl3d_runner_apply_state_json_file(sdl3d_properties *props, const char *path, char *error_buffer,
                                            int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif

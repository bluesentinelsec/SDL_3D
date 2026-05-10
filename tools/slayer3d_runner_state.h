/**
 * @file slayer3d_runner_state.h
 * @brief Scene-state parsing helpers for slayer3d_runner direct scene launches.
 */

#ifndef SLAYER3D_RUNNER_STATE_H
#define SLAYER3D_RUNNER_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "slayer3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool slayer3d_runner_apply_state_assignment(slayer3d_properties *props, const char *assignment, char *error_buffer,
                                                int error_buffer_size);
    bool slayer3d_runner_apply_state_json_object(slayer3d_properties *props, const char *json, size_t json_size,
                                                 const char *source_name, char *error_buffer, int error_buffer_size);
    bool slayer3d_runner_apply_state_json_file(slayer3d_properties *props, const char *path, char *error_buffer,
                                               int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif

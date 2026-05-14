/**
 * @file slayer3d_runner_manifest.h
 * @brief Editor test-run manifest helpers for slayer3d_runner.
 */

#ifndef SLAYER3D_RUNNER_MANIFEST_H
#define SLAYER3D_RUNNER_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

#include "slayer3d_runner_cli.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool slayer3d_runner_apply_test_run_manifest_json(slayer3d_runner_args *args, const char *json, size_t json_size,
                                                      const char *source_name, char *error_buffer,
                                                      int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif

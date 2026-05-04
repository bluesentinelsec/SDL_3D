#ifndef SDL3D_NETWORK_REPLICATION_SCHEMA_H
#define SDL3D_NETWORK_REPLICATION_SCHEMA_H

#include <stdbool.h>

#include "sdl3d/network_replication.h"
#include "yyjson.h"

typedef struct sdl3d_replication_field_descriptor
{
    const char *path;
    sdl3d_replication_field_type type;
} sdl3d_replication_field_descriptor;

bool sdl3d_replication_field_type_from_string(const char *type, sdl3d_replication_field_type *out_type);
const char *sdl3d_replication_field_type_name(sdl3d_replication_field_type type);
bool sdl3d_replication_builtin_field_type(const char *field, sdl3d_replication_field_type *out_type);
bool sdl3d_replication_field_descriptor_from_json(yyjson_val *field,
                                                  sdl3d_replication_field_descriptor *out_descriptor);

#endif

#ifndef SLAYER3D_NETWORK_REPLICATION_SCHEMA_H
#define SLAYER3D_NETWORK_REPLICATION_SCHEMA_H

#include <stdbool.h>

#include "slayer3d/network_replication.h"
#include "yyjson.h"

typedef struct slayer3d_replication_field_descriptor
{
    const char *path;
    slayer3d_replication_field_type type;
} slayer3d_replication_field_descriptor;

bool slayer3d_replication_field_type_from_string(const char *type, slayer3d_replication_field_type *out_type);
const char *slayer3d_replication_field_type_name(slayer3d_replication_field_type type);
bool slayer3d_replication_builtin_field_type(const char *field, slayer3d_replication_field_type *out_type);
bool slayer3d_replication_field_descriptor_from_json(yyjson_val *field,
                                                     slayer3d_replication_field_descriptor *out_descriptor);

#endif

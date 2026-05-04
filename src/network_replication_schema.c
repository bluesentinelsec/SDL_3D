#include "network_replication_schema.h"

#include <SDL3/SDL_stdinc.h>

bool sdl3d_replication_field_type_from_string(const char *type, sdl3d_replication_field_type *out_type)
{
    if (type == NULL || type[0] == '\0')
        return false;
    if (SDL_strcmp(type, "bool") == 0 || SDL_strcmp(type, "boolean") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_BOOL;
        return true;
    }
    if (SDL_strcmp(type, "int32") == 0 || SDL_strcmp(type, "int") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_INT32;
        return true;
    }
    if (SDL_strcmp(type, "float32") == 0 || SDL_strcmp(type, "float") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_FLOAT32;
        return true;
    }
    if (SDL_strcmp(type, "enum_id") == 0 || SDL_strcmp(type, "string_id") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_ENUM_ID;
        return true;
    }
    if (SDL_strcmp(type, "vec2") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_VEC2;
        return true;
    }
    if (SDL_strcmp(type, "vec3") == 0)
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_VEC3;
        return true;
    }
    return false;
}

const char *sdl3d_replication_field_type_name(sdl3d_replication_field_type type)
{
    switch (type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        return "bool";
    case SDL3D_REPLICATION_FIELD_INT32:
        return "int32";
    case SDL3D_REPLICATION_FIELD_FLOAT32:
        return "float32";
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        return "enum_id";
    case SDL3D_REPLICATION_FIELD_VEC2:
        return "vec2";
    case SDL3D_REPLICATION_FIELD_VEC3:
        return "vec3";
    default:
        return "invalid";
    }
}

bool sdl3d_replication_builtin_field_type(const char *field, sdl3d_replication_field_type *out_type)
{
    if (field != NULL &&
        (SDL_strcmp(field, "position") == 0 || SDL_strcmp(field, "rotation") == 0 || SDL_strcmp(field, "scale") == 0))
    {
        if (out_type != NULL)
            *out_type = SDL3D_REPLICATION_FIELD_VEC3;
        return true;
    }
    return false;
}

static yyjson_val *schema_obj_get(yyjson_val *object, const char *key)
{
    return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : NULL;
}

static const char *schema_json_string(yyjson_val *object, const char *key)
{
    yyjson_val *value = schema_obj_get(object, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

bool sdl3d_replication_field_descriptor_from_json(yyjson_val *field, sdl3d_replication_field_descriptor *out_descriptor)
{
    if (out_descriptor != NULL)
    {
        out_descriptor->path = NULL;
        out_descriptor->type = SDL3D_REPLICATION_FIELD_BOOL;
    }

    if (yyjson_is_str(field))
    {
        const char *path = yyjson_get_str(field);
        sdl3d_replication_field_type type = SDL3D_REPLICATION_FIELD_VEC3;
        if (!sdl3d_replication_builtin_field_type(path, &type))
            return false;
        if (out_descriptor != NULL)
        {
            out_descriptor->path = path;
            out_descriptor->type = type;
        }
        return true;
    }

    if (!yyjson_is_obj(field))
        return false;

    const char *path = schema_json_string(field, "path");
    if (path == NULL)
        path = schema_json_string(field, "field");
    sdl3d_replication_field_type type = SDL3D_REPLICATION_FIELD_BOOL;
    if (path == NULL || path[0] == '\0' ||
        !sdl3d_replication_field_type_from_string(schema_json_string(field, "type"), &type))
    {
        return false;
    }

    if (out_descriptor != NULL)
    {
        out_descriptor->path = path;
        out_descriptor->type = type;
    }
    return true;
}

#include "sdl3d/network_replication.h"

#include <string.h>

_Static_assert(sizeof(Sint32) == sizeof(Uint32), "SDL3D replication requires 32-bit signed integers");
_Static_assert(sizeof(float) == sizeof(Uint32), "SDL3D replication requires 32-bit floats");

static bool sdl3d_replication_field_type_is_valid(sdl3d_replication_field_type type)
{
    return type == SDL3D_REPLICATION_FIELD_BOOL || type == SDL3D_REPLICATION_FIELD_INT32 ||
           type == SDL3D_REPLICATION_FIELD_FLOAT32 || type == SDL3D_REPLICATION_FIELD_ENUM_ID ||
           type == SDL3D_REPLICATION_FIELD_VEC2 || type == SDL3D_REPLICATION_FIELD_VEC3;
}

static bool sdl3d_replication_can_write(const sdl3d_replication_writer *writer, size_t byte_count)
{
    if (writer == NULL || (writer->buffer == NULL && writer->capacity > 0) || writer->offset > writer->capacity)
    {
        return false;
    }
    return byte_count <= writer->capacity - writer->offset;
}

static bool sdl3d_replication_can_read(const sdl3d_replication_reader *reader, size_t byte_count)
{
    if (reader == NULL || (reader->buffer == NULL && reader->size > 0) || reader->offset > reader->size)
    {
        return false;
    }
    return byte_count <= reader->size - reader->offset;
}

static bool sdl3d_replication_write_u8(sdl3d_replication_writer *writer, Uint8 value)
{
    if (!sdl3d_replication_can_write(writer, 1U))
    {
        return false;
    }

    writer->buffer[writer->offset] = value;
    writer->offset += 1U;
    return true;
}

static bool sdl3d_replication_read_u8(sdl3d_replication_reader *reader, Uint8 *out_value)
{
    if (out_value == NULL || !sdl3d_replication_can_read(reader, 1U))
    {
        return false;
    }

    *out_value = reader->buffer[reader->offset];
    reader->offset += 1U;
    return true;
}

static bool sdl3d_replication_write_u32(sdl3d_replication_writer *writer, Uint32 value)
{
    if (!sdl3d_replication_can_write(writer, 4U))
    {
        return false;
    }

    writer->buffer[writer->offset + 0U] = (Uint8)(value & 0xFFU);
    writer->buffer[writer->offset + 1U] = (Uint8)((value >> 8U) & 0xFFU);
    writer->buffer[writer->offset + 2U] = (Uint8)((value >> 16U) & 0xFFU);
    writer->buffer[writer->offset + 3U] = (Uint8)((value >> 24U) & 0xFFU);
    writer->offset += 4U;
    return true;
}

static bool sdl3d_replication_read_u32(sdl3d_replication_reader *reader, Uint32 *out_value)
{
    if (out_value == NULL || !sdl3d_replication_can_read(reader, 4U))
    {
        return false;
    }

    *out_value = (Uint32)reader->buffer[reader->offset + 0U] | ((Uint32)reader->buffer[reader->offset + 1U] << 8U) |
                 ((Uint32)reader->buffer[reader->offset + 2U] << 16U) |
                 ((Uint32)reader->buffer[reader->offset + 3U] << 24U);
    reader->offset += 4U;
    return true;
}

void sdl3d_replication_writer_init(sdl3d_replication_writer *writer, void *buffer, size_t capacity)
{
    if (writer == NULL)
    {
        return;
    }

    writer->buffer = (Uint8 *)buffer;
    writer->capacity = capacity;
    writer->offset = 0U;
}

size_t sdl3d_replication_writer_offset(const sdl3d_replication_writer *writer)
{
    return writer != NULL ? writer->offset : 0U;
}

size_t sdl3d_replication_writer_remaining(const sdl3d_replication_writer *writer)
{
    if (writer == NULL || writer->offset > writer->capacity)
    {
        return 0U;
    }
    return writer->capacity - writer->offset;
}

size_t sdl3d_replication_field_wire_size(sdl3d_replication_field_type type)
{
    switch (type)
    {
    case SDL3D_REPLICATION_FIELD_BOOL:
        return 1U;
    case SDL3D_REPLICATION_FIELD_INT32:
    case SDL3D_REPLICATION_FIELD_FLOAT32:
    case SDL3D_REPLICATION_FIELD_ENUM_ID:
        return 4U;
    case SDL3D_REPLICATION_FIELD_VEC2:
        return 8U;
    case SDL3D_REPLICATION_FIELD_VEC3:
        return 12U;
    default:
        return 0U;
    }
}

bool sdl3d_replication_write_field_type(sdl3d_replication_writer *writer, sdl3d_replication_field_type type)
{
    if (!sdl3d_replication_field_type_is_valid(type))
    {
        return false;
    }
    return sdl3d_replication_write_u8(writer, (Uint8)type);
}

bool sdl3d_replication_write_bool(sdl3d_replication_writer *writer, bool value)
{
    return sdl3d_replication_write_u8(writer, value ? 1U : 0U);
}

bool sdl3d_replication_write_int32(sdl3d_replication_writer *writer, Sint32 value)
{
    Uint32 bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return sdl3d_replication_write_u32(writer, bits);
}

bool sdl3d_replication_write_float32(sdl3d_replication_writer *writer, float value)
{
    Uint32 bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return sdl3d_replication_write_u32(writer, bits);
}

bool sdl3d_replication_write_enum_id(sdl3d_replication_writer *writer, Sint32 value)
{
    return sdl3d_replication_write_int32(writer, value);
}

bool sdl3d_replication_write_vec2(sdl3d_replication_writer *writer, sdl3d_vec2 value)
{
    const size_t offset = writer != NULL ? writer->offset : 0U;
    if (!sdl3d_replication_can_write(writer, 8U))
    {
        return false;
    }
    if (!sdl3d_replication_write_float32(writer, value.x) || !sdl3d_replication_write_float32(writer, value.y))
    {
        writer->offset = offset;
        return false;
    }
    return true;
}

bool sdl3d_replication_write_vec3(sdl3d_replication_writer *writer, sdl3d_vec3 value)
{
    const size_t offset = writer != NULL ? writer->offset : 0U;
    if (!sdl3d_replication_can_write(writer, 12U))
    {
        return false;
    }
    if (!sdl3d_replication_write_float32(writer, value.x) || !sdl3d_replication_write_float32(writer, value.y) ||
        !sdl3d_replication_write_float32(writer, value.z))
    {
        writer->offset = offset;
        return false;
    }
    return true;
}

void sdl3d_replication_reader_init(sdl3d_replication_reader *reader, const void *buffer, size_t size)
{
    if (reader == NULL)
    {
        return;
    }

    reader->buffer = (const Uint8 *)buffer;
    reader->size = size;
    reader->offset = 0U;
}

size_t sdl3d_replication_reader_offset(const sdl3d_replication_reader *reader)
{
    return reader != NULL ? reader->offset : 0U;
}

size_t sdl3d_replication_reader_remaining(const sdl3d_replication_reader *reader)
{
    if (reader == NULL || reader->offset > reader->size)
    {
        return 0U;
    }
    return reader->size - reader->offset;
}

bool sdl3d_replication_read_field_type(sdl3d_replication_reader *reader, sdl3d_replication_field_type *out_type)
{
    if (out_type == NULL || !sdl3d_replication_can_read(reader, 1U))
    {
        return false;
    }

    const size_t offset = reader->offset;
    Uint8 value = 0U;
    if (!sdl3d_replication_read_u8(reader, &value))
    {
        return false;
    }

    const sdl3d_replication_field_type type = (sdl3d_replication_field_type)value;
    if (!sdl3d_replication_field_type_is_valid(type))
    {
        reader->offset = offset;
        return false;
    }

    *out_type = type;
    return true;
}

bool sdl3d_replication_read_bool(sdl3d_replication_reader *reader, bool *out_value)
{
    if (out_value == NULL || !sdl3d_replication_can_read(reader, 1U))
    {
        return false;
    }

    const size_t offset = reader->offset;
    Uint8 value = 0U;
    if (!sdl3d_replication_read_u8(reader, &value))
    {
        return false;
    }
    if (value > 1U)
    {
        reader->offset = offset;
        return false;
    }

    *out_value = value != 0U;
    return true;
}

bool sdl3d_replication_read_int32(sdl3d_replication_reader *reader, Sint32 *out_value)
{
    if (out_value == NULL)
    {
        return false;
    }

    Uint32 value = 0U;
    if (!sdl3d_replication_read_u32(reader, &value))
    {
        return false;
    }

    memcpy(out_value, &value, sizeof(value));
    return true;
}

bool sdl3d_replication_read_float32(sdl3d_replication_reader *reader, float *out_value)
{
    if (out_value == NULL)
    {
        return false;
    }

    Uint32 bits = 0U;
    if (!sdl3d_replication_read_u32(reader, &bits))
    {
        return false;
    }

    memcpy(out_value, &bits, sizeof(bits));
    return true;
}

bool sdl3d_replication_read_enum_id(sdl3d_replication_reader *reader, Sint32 *out_value)
{
    return sdl3d_replication_read_int32(reader, out_value);
}

bool sdl3d_replication_read_vec2(sdl3d_replication_reader *reader, sdl3d_vec2 *out_value)
{
    if (out_value == NULL || !sdl3d_replication_can_read(reader, 8U))
    {
        return false;
    }

    const size_t offset = reader->offset;
    sdl3d_vec2 value = {0.0f, 0.0f};
    if (!sdl3d_replication_read_float32(reader, &value.x) || !sdl3d_replication_read_float32(reader, &value.y))
    {
        reader->offset = offset;
        return false;
    }

    *out_value = value;
    return true;
}

bool sdl3d_replication_read_vec3(sdl3d_replication_reader *reader, sdl3d_vec3 *out_value)
{
    if (out_value == NULL || !sdl3d_replication_can_read(reader, 12U))
    {
        return false;
    }

    const size_t offset = reader->offset;
    sdl3d_vec3 value = {0.0f, 0.0f, 0.0f};
    if (!sdl3d_replication_read_float32(reader, &value.x) || !sdl3d_replication_read_float32(reader, &value.y) ||
        !sdl3d_replication_read_float32(reader, &value.z))
    {
        reader->offset = offset;
        return false;
    }

    *out_value = value;
    return true;
}

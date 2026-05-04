/**
 * @file network_replication.h
 * @brief Deterministic field codec for data-driven network replication.
 *
 * The replication codec is transport independent. It writes and reads scalar
 * and vector field values using a strict little-endian wire format so higher
 * layers can build authored replication schemas without embedding per-game
 * serialization code in C.
 */

#ifndef SDL3D_NETWORK_REPLICATION_H
#define SDL3D_NETWORK_REPLICATION_H

#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>
#include <stddef.h>

#include "sdl3d/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Primitive field types supported by the replication codec. */
    typedef enum sdl3d_replication_field_type
    {
        SDL3D_REPLICATION_FIELD_BOOL = 1,    /**< One-byte boolean, only 0 or 1 are valid. */
        SDL3D_REPLICATION_FIELD_INT32 = 2,   /**< Signed 32-bit integer. */
        SDL3D_REPLICATION_FIELD_FLOAT32 = 3, /**< IEEE-754 32-bit float. */
        SDL3D_REPLICATION_FIELD_ENUM_ID = 4, /**< Signed 32-bit enum or stable string-id value. */
        SDL3D_REPLICATION_FIELD_VEC2 = 5,    /**< Two float32 values: x, y. */
        SDL3D_REPLICATION_FIELD_VEC3 = 6,    /**< Three float32 values: x, y, z. */
    } sdl3d_replication_field_type;

    /** @brief Bounded writer for replication packets. */
    typedef struct sdl3d_replication_writer
    {
        Uint8 *buffer;   /**< Destination byte buffer. */
        size_t capacity; /**< Destination capacity in bytes. */
        size_t offset;   /**< Current write offset in bytes. */
    } sdl3d_replication_writer;

    /** @brief Bounded reader for replication packets. */
    typedef struct sdl3d_replication_reader
    {
        const Uint8 *buffer; /**< Source byte buffer. */
        size_t size;         /**< Source size in bytes. */
        size_t offset;       /**< Current read offset in bytes. */
    } sdl3d_replication_reader;

    /**
     * @brief Initialize a packet writer.
     *
     * Passing NULL for @p buffer is valid only when @p capacity is zero.
     */
    void sdl3d_replication_writer_init(sdl3d_replication_writer *writer, void *buffer, size_t capacity);

    /** @brief Return the current writer byte offset, or zero for NULL. */
    size_t sdl3d_replication_writer_offset(const sdl3d_replication_writer *writer);

    /** @brief Return remaining writer capacity in bytes, or zero for NULL/invalid state. */
    size_t sdl3d_replication_writer_remaining(const sdl3d_replication_writer *writer);

    /**
     * @brief Return the wire size in bytes for a supported field type.
     *
     * Returns zero for invalid or unknown field types.
     */
    size_t sdl3d_replication_field_wire_size(sdl3d_replication_field_type type);

    /** @brief Write a one-byte replication field type tag. */
    bool sdl3d_replication_write_field_type(sdl3d_replication_writer *writer, sdl3d_replication_field_type type);

    /** @brief Write a one-byte boolean. */
    bool sdl3d_replication_write_bool(sdl3d_replication_writer *writer, bool value);

    /** @brief Write a signed 32-bit integer. */
    bool sdl3d_replication_write_int32(sdl3d_replication_writer *writer, Sint32 value);

    /** @brief Write a 32-bit float. */
    bool sdl3d_replication_write_float32(sdl3d_replication_writer *writer, float value);

    /** @brief Write a signed 32-bit enum or stable string-id value. */
    bool sdl3d_replication_write_enum_id(sdl3d_replication_writer *writer, Sint32 value);

    /** @brief Write a vec2 as two 32-bit floats. */
    bool sdl3d_replication_write_vec2(sdl3d_replication_writer *writer, sdl3d_vec2 value);

    /** @brief Write a vec3 as three 32-bit floats. */
    bool sdl3d_replication_write_vec3(sdl3d_replication_writer *writer, sdl3d_vec3 value);

    /** @brief Initialize a packet reader. */
    void sdl3d_replication_reader_init(sdl3d_replication_reader *reader, const void *buffer, size_t size);

    /** @brief Return the current reader byte offset, or zero for NULL. */
    size_t sdl3d_replication_reader_offset(const sdl3d_replication_reader *reader);

    /** @brief Return unread byte count, or zero for NULL/invalid state. */
    size_t sdl3d_replication_reader_remaining(const sdl3d_replication_reader *reader);

    /**
     * @brief Read and validate a one-byte replication field type tag.
     *
     * Invalid or unknown tags fail and leave the reader offset unchanged.
     */
    bool sdl3d_replication_read_field_type(sdl3d_replication_reader *reader, sdl3d_replication_field_type *out_type);

    /**
     * @brief Read a one-byte boolean.
     *
     * Values other than 0 and 1 fail and leave the reader offset unchanged.
     */
    bool sdl3d_replication_read_bool(sdl3d_replication_reader *reader, bool *out_value);

    /** @brief Read a signed 32-bit integer. */
    bool sdl3d_replication_read_int32(sdl3d_replication_reader *reader, Sint32 *out_value);

    /** @brief Read a 32-bit float. */
    bool sdl3d_replication_read_float32(sdl3d_replication_reader *reader, float *out_value);

    /** @brief Read a signed 32-bit enum or stable string-id value. */
    bool sdl3d_replication_read_enum_id(sdl3d_replication_reader *reader, Sint32 *out_value);

    /** @brief Read a vec2 encoded as two 32-bit floats. */
    bool sdl3d_replication_read_vec2(sdl3d_replication_reader *reader, sdl3d_vec2 *out_value);

    /** @brief Read a vec3 encoded as three 32-bit floats. */
    bool sdl3d_replication_read_vec3(sdl3d_replication_reader *reader, sdl3d_vec3 *out_value);

#ifdef __cplusplus
}
#endif

#endif

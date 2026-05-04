#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

extern "C"
{
#include "sdl3d/network_replication.h"
}

namespace
{
constexpr float kTolerance = 1e-6f;

Uint32 float_bits(float value)
{
    Uint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float test helper requires 32-bit floats");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void expect_vec2_near(sdl3d_vec2 actual, sdl3d_vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
}

void expect_vec3_near(sdl3d_vec3 actual, sdl3d_vec3 expected)
{
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
}
} // namespace

TEST(SDL3DNetworkReplication, WriterAndReaderTrackSize)
{
    std::array<Uint8, 16> buffer{};

    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());
    EXPECT_EQ(sdl3d_replication_writer_offset(&writer), 0U);
    EXPECT_EQ(sdl3d_replication_writer_remaining(&writer), buffer.size());

    ASSERT_TRUE(sdl3d_replication_write_bool(&writer, true));
    EXPECT_EQ(sdl3d_replication_writer_offset(&writer), 1U);
    EXPECT_EQ(sdl3d_replication_writer_remaining(&writer), buffer.size() - 1U);

    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), sdl3d_replication_writer_offset(&writer));
    EXPECT_EQ(sdl3d_replication_reader_offset(&reader), 0U);
    EXPECT_EQ(sdl3d_replication_reader_remaining(&reader), 1U);

    bool value = false;
    ASSERT_TRUE(sdl3d_replication_read_bool(&reader, &value));
    EXPECT_TRUE(value);
    EXPECT_EQ(sdl3d_replication_reader_offset(&reader), 1U);
    EXPECT_EQ(sdl3d_replication_reader_remaining(&reader), 0U);
}

TEST(SDL3DNetworkReplication, RoundTripsSupportedFieldValues)
{
    std::array<Uint8, 128> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());

    const sdl3d_vec2 expected_vec2 = {1.25f, -2.5f};
    const sdl3d_vec3 expected_vec3 = {3.0f, -4.0f, 5.5f};
    ASSERT_TRUE(sdl3d_replication_write_field_type(&writer, SDL3D_REPLICATION_FIELD_VEC3));
    ASSERT_TRUE(sdl3d_replication_write_bool(&writer, true));
    ASSERT_TRUE(sdl3d_replication_write_bool(&writer, false));
    ASSERT_TRUE(sdl3d_replication_write_int32(&writer, -42));
    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, 3.5f));
    ASSERT_TRUE(sdl3d_replication_write_enum_id(&writer, 17));
    ASSERT_TRUE(sdl3d_replication_write_vec2(&writer, expected_vec2));
    ASSERT_TRUE(sdl3d_replication_write_vec3(&writer, expected_vec3));

    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), sdl3d_replication_writer_offset(&writer));

    sdl3d_replication_field_type type = SDL3D_REPLICATION_FIELD_BOOL;
    bool bool_value = false;
    bool false_value = true;
    Sint32 int_value = 0;
    float float_value = 0.0f;
    Sint32 enum_id_value = 0;
    sdl3d_vec2 vec2_value = {};
    sdl3d_vec3 vec3_value = {};

    ASSERT_TRUE(sdl3d_replication_read_field_type(&reader, &type));
    EXPECT_EQ(type, SDL3D_REPLICATION_FIELD_VEC3);
    ASSERT_TRUE(sdl3d_replication_read_bool(&reader, &bool_value));
    EXPECT_TRUE(bool_value);
    ASSERT_TRUE(sdl3d_replication_read_bool(&reader, &false_value));
    EXPECT_FALSE(false_value);
    ASSERT_TRUE(sdl3d_replication_read_int32(&reader, &int_value));
    EXPECT_EQ(int_value, -42);
    ASSERT_TRUE(sdl3d_replication_read_float32(&reader, &float_value));
    EXPECT_NEAR(float_value, 3.5f, kTolerance);
    ASSERT_TRUE(sdl3d_replication_read_enum_id(&reader, &enum_id_value));
    EXPECT_EQ(enum_id_value, 17);
    ASSERT_TRUE(sdl3d_replication_read_vec2(&reader, &vec2_value));
    expect_vec2_near(vec2_value, expected_vec2);
    ASSERT_TRUE(sdl3d_replication_read_vec3(&reader, &vec3_value));
    expect_vec3_near(vec3_value, expected_vec3);
    EXPECT_EQ(sdl3d_replication_reader_remaining(&reader), 0U);
}

TEST(SDL3DNetworkReplication, ReportsFieldWireSizes)
{
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_BOOL), 1U);
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_INT32), 4U);
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_FLOAT32), 4U);
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_ENUM_ID), 4U);
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_VEC2), 8U);
    EXPECT_EQ(sdl3d_replication_field_wire_size(SDL3D_REPLICATION_FIELD_VEC3), 12U);
    EXPECT_EQ(sdl3d_replication_field_wire_size((sdl3d_replication_field_type)255), 0U);
}

TEST(SDL3DNetworkReplication, RoundTripsBoundaryIntegers)
{
    std::array<Uint8, 8> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());

    ASSERT_TRUE(sdl3d_replication_write_int32(&writer, std::numeric_limits<Sint32>::min()));
    ASSERT_TRUE(sdl3d_replication_write_int32(&writer, std::numeric_limits<Sint32>::max()));

    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), sdl3d_replication_writer_offset(&writer));

    Sint32 min_value = 0;
    Sint32 max_value = 0;
    ASSERT_TRUE(sdl3d_replication_read_int32(&reader, &min_value));
    ASSERT_TRUE(sdl3d_replication_read_int32(&reader, &max_value));
    EXPECT_EQ(min_value, std::numeric_limits<Sint32>::min());
    EXPECT_EQ(max_value, std::numeric_limits<Sint32>::max());
}

TEST(SDL3DNetworkReplication, PreservesFloatBitPatterns)
{
    std::array<Uint8, 16> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());

    const float negative_zero = -0.0f;
    const float infinity = std::numeric_limits<float>::infinity();
    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float negative_value = -123.5f;

    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, negative_zero));
    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, infinity));
    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, quiet_nan));
    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, negative_value));

    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), sdl3d_replication_writer_offset(&writer));

    float out_negative_zero = 1.0f;
    float out_infinity = 0.0f;
    float out_nan = 0.0f;
    float out_negative_value = 0.0f;
    ASSERT_TRUE(sdl3d_replication_read_float32(&reader, &out_negative_zero));
    ASSERT_TRUE(sdl3d_replication_read_float32(&reader, &out_infinity));
    ASSERT_TRUE(sdl3d_replication_read_float32(&reader, &out_nan));
    ASSERT_TRUE(sdl3d_replication_read_float32(&reader, &out_negative_value));

    EXPECT_EQ(float_bits(out_negative_zero), float_bits(negative_zero));
    EXPECT_EQ(float_bits(out_infinity), float_bits(infinity));
    EXPECT_EQ(float_bits(out_nan), float_bits(quiet_nan));
    EXPECT_EQ(float_bits(out_negative_value), float_bits(negative_value));
}

TEST(SDL3DNetworkReplication, UsesDeterministicLittleEndianEncoding)
{
    std::array<Uint8, 8> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());

    ASSERT_TRUE(sdl3d_replication_write_int32(&writer, 0x01020304));
    ASSERT_TRUE(sdl3d_replication_write_float32(&writer, 1.0f));

    EXPECT_EQ(buffer[0], 0x04);
    EXPECT_EQ(buffer[1], 0x03);
    EXPECT_EQ(buffer[2], 0x02);
    EXPECT_EQ(buffer[3], 0x01);
    EXPECT_EQ(buffer[4], 0x00);
    EXPECT_EQ(buffer[5], 0x00);
    EXPECT_EQ(buffer[6], 0x80);
    EXPECT_EQ(buffer[7], 0x3f);
}

TEST(SDL3DNetworkReplication, RejectsTruncatedWritesWithoutAdvancing)
{
    std::array<Uint8, 8> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());

    ASSERT_TRUE(sdl3d_replication_write_int32(&writer, 1234));
    const size_t offset = sdl3d_replication_writer_offset(&writer);
    const sdl3d_vec3 value = {1.0f, 2.0f, 3.0f};
    EXPECT_FALSE(sdl3d_replication_write_vec3(&writer, value));
    EXPECT_EQ(sdl3d_replication_writer_offset(&writer), offset);
}

TEST(SDL3DNetworkReplication, RejectsZeroCapacityWrites)
{
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, nullptr, 0U);

    EXPECT_EQ(sdl3d_replication_writer_offset(&writer), 0U);
    EXPECT_EQ(sdl3d_replication_writer_remaining(&writer), 0U);
    EXPECT_FALSE(sdl3d_replication_write_bool(&writer, false));
    EXPECT_FALSE(sdl3d_replication_write_int32(&writer, 1));
    EXPECT_FALSE(sdl3d_replication_write_float32(&writer, 1.0f));
    EXPECT_FALSE(sdl3d_replication_write_enum_id(&writer, 1));
    EXPECT_FALSE(sdl3d_replication_write_vec2(&writer, {1.0f, 2.0f}));
    EXPECT_FALSE(sdl3d_replication_write_vec3(&writer, {1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(sdl3d_replication_writer_offset(&writer), 0U);
}

TEST(SDL3DNetworkReplication, RejectsTruncatedReadsWithoutAdvancing)
{
    const std::array<Uint8, 7> buffer = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00};
    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), buffer.size());

    Sint32 first = 0;
    ASSERT_TRUE(sdl3d_replication_read_int32(&reader, &first));
    EXPECT_EQ(first, 1);
    const size_t offset = sdl3d_replication_reader_offset(&reader);

    Sint32 second = 0;
    EXPECT_FALSE(sdl3d_replication_read_int32(&reader, &second));
    EXPECT_EQ(sdl3d_replication_reader_offset(&reader), offset);
}

TEST(SDL3DNetworkReplication, RejectsInvalidBoolAndFieldTypeWithoutAdvancing)
{
    const std::array<Uint8, 2> buffer = {2U, 255U};

    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), buffer.size());

    bool bool_value = false;
    EXPECT_FALSE(sdl3d_replication_read_bool(&reader, &bool_value));
    EXPECT_EQ(sdl3d_replication_reader_offset(&reader), 0U);

    reader.offset = 1U;
    sdl3d_replication_field_type type = SDL3D_REPLICATION_FIELD_BOOL;
    EXPECT_FALSE(sdl3d_replication_read_field_type(&reader, &type));
    EXPECT_EQ(sdl3d_replication_reader_offset(&reader), 1U);
}

TEST(SDL3DNetworkReplication, RejectsInvalidArguments)
{
    std::array<Uint8, 4> buffer{};
    sdl3d_replication_writer writer{};
    sdl3d_replication_writer_init(&writer, buffer.data(), buffer.size());
    sdl3d_replication_reader reader{};
    sdl3d_replication_reader_init(&reader, buffer.data(), buffer.size());

    EXPECT_FALSE(sdl3d_replication_write_bool(nullptr, true));
    EXPECT_FALSE(sdl3d_replication_write_field_type(&writer, (sdl3d_replication_field_type)255));

    EXPECT_FALSE(sdl3d_replication_read_bool(nullptr, nullptr));
    EXPECT_FALSE(sdl3d_replication_read_bool(&reader, nullptr));
}

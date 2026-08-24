#include "handheld_control_protocol.h"

#include <float.h>
#include <string.h>

_Static_assert(sizeof(float) == sizeof(uint32_t),
               "RFHC v1 requires IEEE-754 binary32 float storage");

static void write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void write_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void write_be64(uint8_t *destination, uint64_t value)
{
    write_be32(destination, (uint32_t)(value >> 32));
    write_be32(destination + 4, (uint32_t)value);
}

static void write_be_float32(uint8_t *destination, float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    write_be32(destination, bits);
}

static int is_finite_float(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static int is_valid_quaternion(const handheld_control_packet_t *packet)
{
    const float x = packet->quaternion_x;
    const float y = packet->quaternion_y;
    const float z = packet->quaternion_z;
    const float w = packet->quaternion_w;
    if (!is_finite_float(x) || !is_finite_float(y) ||
        !is_finite_float(z) || !is_finite_float(w)) {
        return 0;
    }

    const float norm_squared = x * x + y * y + z * z + w * w;
    return norm_squared >= 0.9409f && norm_squared <= 1.0609f;
}

uint32_t handheld_control_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t reflected_polynomial = UINT32_C(0xEDB88320);
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (reflected_polynomial & mask);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

handheld_control_serialize_result_t handheld_control_serialize(
    const handheld_control_packet_t *packet,
    uint8_t output[HANDHELD_CONTROL_PACKET_SIZE])
{
    if (packet == NULL || output == NULL) {
        return HANDHELD_CONTROL_SERIALIZE_BAD_ARGUMENT;
    }
    if ((packet->flags & (uint8_t)~HANDHELD_CONTROL_FLAG_MASK) != 0) {
        return HANDHELD_CONTROL_SERIALIZE_BAD_FLAGS;
    }
    if (packet->device_id == 0) {
        return HANDHELD_CONTROL_SERIALIZE_BAD_DEVICE_ID;
    }
    if (packet->session_id == 0) {
        return HANDHELD_CONTROL_SERIALIZE_BAD_SESSION_ID;
    }
    if ((packet->flags & HANDHELD_CONTROL_FLAG_ORIENTATION_VALID) != 0 &&
        !is_valid_quaternion(packet)) {
        return HANDHELD_CONTROL_SERIALIZE_BAD_QUATERNION;
    }

    memset(output, 0, HANDHELD_CONTROL_PACKET_SIZE);
    write_be32(output, HANDHELD_CONTROL_MAGIC);
    output[4] = HANDHELD_CONTROL_VERSION;
    output[5] = packet->flags;
    write_be16(output + 6, HANDHELD_CONTROL_PACKET_SIZE);
    write_be32(output + 8, packet->device_id);
    write_be32(output + 12, packet->session_id);
    write_be32(output + 16, packet->sample_seq);
    write_be32(output + 20, packet->event_seq);
    write_be64(output + 24, packet->timestamp_ms);
    write_be_float32(output + 32, packet->quaternion_x);
    write_be_float32(output + 36, packet->quaternion_y);
    write_be_float32(output + 40, packet->quaternion_z);
    write_be_float32(output + 44, packet->quaternion_w);
    write_be32(output + 48, handheld_control_crc32(output, 48));
    return HANDHELD_CONTROL_SERIALIZE_OK;
}

const char *handheld_control_serialize_result_name(
    handheld_control_serialize_result_t result)
{
    switch (result) {
    case HANDHELD_CONTROL_SERIALIZE_OK: return "ok";
    case HANDHELD_CONTROL_SERIALIZE_BAD_ARGUMENT: return "bad_argument";
    case HANDHELD_CONTROL_SERIALIZE_BAD_FLAGS: return "bad_flags";
    case HANDHELD_CONTROL_SERIALIZE_BAD_DEVICE_ID: return "bad_device_id";
    case HANDHELD_CONTROL_SERIALIZE_BAD_SESSION_ID: return "bad_session_id";
    case HANDHELD_CONTROL_SERIALIZE_BAD_QUATERNION: return "bad_quaternion";
    default: return "unknown";
    }
}


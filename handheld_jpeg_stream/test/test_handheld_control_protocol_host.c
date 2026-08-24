#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handheld_control_protocol.h"

static const uint8_t NETWORK_BACKEND_TEST_VECTOR[HANDHELD_CONTROL_PACKET_SIZE] = {
    0x52, 0x46, 0x48, 0x43, 0x01, 0x01, 0x00, 0x34,
    0x00, 0x00, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
    0x0A, 0xE9, 0x27, 0xE5,
};

static handheld_control_packet_t make_identity_packet(void)
{
    const handheld_control_packet_t packet = {
        .flags = HANDHELD_CONTROL_FLAG_ORIENTATION_VALID,
        .device_id = 1,
        .session_id = UINT32_C(0x12345678),
        .sample_seq = 1,
        .event_seq = 0,
        .timestamp_ms = 0,
        .quaternion_x = 0.0f,
        .quaternion_y = 0.0f,
        .quaternion_z = 0.0f,
        .quaternion_w = 1.0f,
    };
    return packet;
}

int main(void)
{
    assert(handheld_control_crc32((const uint8_t *)"123456789", 9) ==
           UINT32_C(0xCBF43926));

    handheld_control_packet_t packet = make_identity_packet();
    uint8_t serialized[HANDHELD_CONTROL_PACKET_SIZE];
    assert(handheld_control_serialize(&packet, serialized) ==
           HANDHELD_CONTROL_SERIALIZE_OK);
    assert(memcmp(serialized, NETWORK_BACKEND_TEST_VECTOR,
                  sizeof(serialized)) == 0);

    packet.flags = UINT8_C(0x80);
    assert(handheld_control_serialize(&packet, serialized) ==
           HANDHELD_CONTROL_SERIALIZE_BAD_FLAGS);

    packet = make_identity_packet();
    packet.quaternion_w = 0.0f;
    assert(handheld_control_serialize(&packet, serialized) ==
           HANDHELD_CONTROL_SERIALIZE_BAD_QUATERNION);

    packet = make_identity_packet();
    packet.device_id = 0;
    assert(handheld_control_serialize(&packet, serialized) ==
           HANDHELD_CONTROL_SERIALIZE_BAD_DEVICE_ID);

    packet = make_identity_packet();
    packet.session_id = 0;
    assert(handheld_control_serialize(&packet, serialized) ==
           HANDHELD_CONTROL_SERIALIZE_BAD_SESSION_ID);

    puts("6/6 RFHC serializer tests passed; backend vector matched");
    return 0;
}


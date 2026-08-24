#pragma once

#include <stddef.h>
#include <stdint.h>

#define HANDHELD_CONTROL_MAGIC       UINT32_C(0x52464843) /* 'RFHC' */
#define HANDHELD_CONTROL_VERSION     UINT8_C(1)
#define HANDHELD_CONTROL_PACKET_SIZE 52u

#define HANDHELD_CONTROL_FLAG_ORIENTATION_VALID       UINT8_C(0x01)
#define HANDHELD_CONTROL_FLAG_REQUEST_POSITION_UPDATE UINT8_C(0x02)
#define HANDHELD_CONTROL_FLAG_RECENTER_ORIENTATION    UINT8_C(0x04)
#define HANDHELD_CONTROL_FLAG_TIME_SYNCED             UINT8_C(0x08)
#define HANDHELD_CONTROL_FLAG_MASK                    UINT8_C(0x0F)

typedef struct {
    uint8_t flags;
    uint32_t device_id;
    uint32_t session_id;
    uint32_t sample_seq;
    uint32_t event_seq;
    uint64_t timestamp_ms;
    float quaternion_x;
    float quaternion_y;
    float quaternion_z;
    float quaternion_w;
} handheld_control_packet_t;

typedef enum {
    HANDHELD_CONTROL_SERIALIZE_OK = 0,
    HANDHELD_CONTROL_SERIALIZE_BAD_ARGUMENT,
    HANDHELD_CONTROL_SERIALIZE_BAD_FLAGS,
    HANDHELD_CONTROL_SERIALIZE_BAD_DEVICE_ID,
    HANDHELD_CONTROL_SERIALIZE_BAD_SESSION_ID,
    HANDHELD_CONTROL_SERIALIZE_BAD_QUATERNION,
} handheld_control_serialize_result_t;

/* CRC-32/ISO-HDLC, also known as CRC32/IEEE. */
uint32_t handheld_control_crc32(const uint8_t *data, size_t length);

handheld_control_serialize_result_t handheld_control_serialize(
    const handheld_control_packet_t *packet,
    uint8_t output[HANDHELD_CONTROL_PACKET_SIZE]);

const char *handheld_control_serialize_result_name(
    handheld_control_serialize_result_t result);


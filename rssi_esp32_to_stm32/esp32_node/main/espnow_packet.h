#ifndef ESPNOW_PACKET_H
#define ESPNOW_PACKET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RSSI_PACKET_MAGIC       0x52465349u
#define RSSI_PACKET_VERSION     2u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t node_id;
    uint16_t payload_len;
    uint32_t seq;
    uint32_t uptime_ms;
    uint64_t measurement_timestamp_ms;
    uint8_t ap_bssid[6];
    int8_t rssi_raw_dbm;
    int16_t rssi_filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
    uint32_t crc32;
} rssi_node_packet_t;

uint32_t rssi_crc32(const void *data, size_t len);
void rssi_packet_finalize(rssi_node_packet_t *packet);
bool rssi_packet_validate(const rssi_node_packet_t *packet);

#endif

#include "espnow_packet.h"

#include <string.h>

uint32_t rssi_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

void rssi_packet_finalize(rssi_node_packet_t *packet)
{
    packet->magic = RSSI_PACKET_MAGIC;
    packet->version = RSSI_PACKET_VERSION;
    packet->payload_len = (uint16_t)sizeof(*packet);
    packet->crc32 = 0;
    packet->crc32 = rssi_crc32(packet, sizeof(*packet));
}

bool rssi_packet_validate(const rssi_node_packet_t *packet)
{
    if (packet == NULL ||
        packet->magic != RSSI_PACKET_MAGIC ||
        packet->version != RSSI_PACKET_VERSION ||
        packet->payload_len != sizeof(*packet)) {
        return false;
    }

    rssi_node_packet_t copy;
    memcpy(&copy, packet, sizeof(copy));
    uint32_t expected = copy.crc32;
    copy.crc32 = 0;

    return rssi_crc32(&copy, sizeof(copy)) == expected;
}

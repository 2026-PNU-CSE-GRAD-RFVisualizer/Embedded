#include "line_protocol.h"

#include <stdio.h>
#include <string.h>

uint8_t line_protocol_xor_checksum(const char *payload)
{
    uint8_t checksum = 0;
    while (*payload != '\0') {
        checksum ^= (uint8_t)*payload++;
    }
    return checksum;
}

static int make_line(char *out, size_t out_len, const char *payload)
{
    uint8_t checksum = line_protocol_xor_checksum(payload);
    return snprintf(out, out_len, "$%s*%02X\n", payload, checksum);
}

int line_protocol_make_rssi_line(char *out, size_t out_len, const rssi_node_packet_t *packet)
{
    char payload[96];
    int written = snprintf(payload,
                           sizeof(payload),
                           "RSSI,%u,%lu,%lu,%d,%d,%u,%u",
                           packet->node_id,
                           (unsigned long)packet->seq,
                           (unsigned long)packet->uptime_ms,
                           packet->rssi_raw_dbm,
                           packet->rssi_filtered_x10,
                           packet->sample_count,
                           packet->error_flags);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return -1;
    }
    return make_line(out, out_len, payload);
}

int line_protocol_make_gwstat_line(char *out,
                                   size_t out_len,
                                   uint32_t uptime_ms,
                                   uint32_t rx_count,
                                   uint32_t crc_error_count,
                                   uint32_t queue_drop_count)
{
    char payload[96];
    int written = snprintf(payload,
                           sizeof(payload),
                           "GWSTAT,%lu,%lu,%lu,%lu",
                           (unsigned long)uptime_ms,
                           (unsigned long)rx_count,
                           (unsigned long)crc_error_count,
                           (unsigned long)queue_drop_count);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return -1;
    }
    return make_line(out, out_len, payload);
}

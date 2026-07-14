#ifndef LINE_PROTOCOL_H
#define LINE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "espnow_packet.h"

uint8_t line_protocol_xor_checksum(const char *payload);
int line_protocol_make_rssi_line(char *out, size_t out_len, const rssi_node_packet_t *packet);
int line_protocol_make_gwstat_line(char *out,
                                   size_t out_len,
                                   uint32_t uptime_ms,
                                   uint32_t rx_count,
                                   uint32_t crc_error_count,
                                   uint32_t queue_drop_count);

#endif

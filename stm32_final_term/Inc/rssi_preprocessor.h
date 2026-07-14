#ifndef RSSI_PREPROCESSOR_H
#define RSSI_PREPROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "rssi_line_parser.h"

#define RSSI_NODE_TIMEOUT_MS    3000u

typedef struct {
    bool active;
    uint8_t node_id;
    uint32_t last_seq;
    uint32_t last_update_ms;
    uint32_t node_uptime_ms;
    int8_t last_raw_rssi;
    int16_t last_filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
    uint32_t received_count;
    uint32_t lost_count;
    uint32_t duplicate_count;
    bool timed_out;
} stm32_node_state_t;

typedef struct {
    stm32_node_state_t nodes[RSSI_MAX_NODES];
    uint32_t checksum_error_count;
    uint32_t format_error_count;
    uint32_t total_received_count;
} rssi_preprocessor_t;

void rssi_preprocessor_init(rssi_preprocessor_t *ctx);
bool rssi_preprocessor_update(rssi_preprocessor_t *ctx,
                              const rssi_measurement_t *measurement,
                              uint32_t now_ms);
void rssi_preprocessor_update_timeouts(rssi_preprocessor_t *ctx, uint32_t now_ms);
uint8_t rssi_preprocessor_active_count(const rssi_preprocessor_t *ctx);

#endif

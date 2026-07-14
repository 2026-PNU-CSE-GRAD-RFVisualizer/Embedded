#ifndef RSSI_FILTER_H
#define RSSI_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#include "node_config.h"

typedef struct {
    int16_t samples[RSSI_FILTER_WINDOW];
    uint8_t count;
    uint8_t write_index;
    int8_t latest_raw;
} rssi_filter_t;

void rssi_filter_init(rssi_filter_t *filter);
bool rssi_filter_add(rssi_filter_t *filter, int8_t raw_dbm);
bool rssi_filter_get_average_x10(const rssi_filter_t *filter, int16_t *out_x10, uint8_t *out_count);

#endif

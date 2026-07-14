#ifndef GATEWAY_RSSI_H
#define GATEWAY_RSSI_H

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int8_t raw_dbm;
    int16_t filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
} gateway_rssi_sample_t;

void gateway_rssi_filter_reset(void);
esp_err_t gateway_rssi_measure_once(gateway_rssi_sample_t *out_sample);

#endif

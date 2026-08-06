#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "sh2_hal.h"

typedef struct {
    uint32_t i2c_error_count;
    uint32_t timeout_count;
    uint32_t short_read_count;
    uint32_t invalid_packet_count;
    uint32_t consecutive_error_count;
} bno085_hal_stats_t;

sh2_Hal_t *bno085_hal_i2c_get(void);
void bno085_hal_i2c_get_stats(bno085_hal_stats_t *out);
esp_err_t bno085_hal_i2c_recreate_bus(void);

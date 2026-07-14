#ifndef RSSI_MEASURE_H
#define RSSI_MEASURE_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t rssi_measure_init_wifi(void);
esp_err_t rssi_measure_scan_target(int8_t *out_rssi_dbm);

#endif

#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *backend_host;
    uint16_t backend_port;
    uint32_t device_id;
} handheld_control_udp_config_t;

esp_err_t handheld_control_udp_start(
    const handheld_control_udp_config_t *config);

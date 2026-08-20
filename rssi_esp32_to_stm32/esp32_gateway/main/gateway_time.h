#ifndef GATEWAY_TIME_H
#define GATEWAY_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t gateway_time_sync_start(void);
uint64_t gateway_time_now_ms(void);
bool gateway_time_is_valid(void);

#endif

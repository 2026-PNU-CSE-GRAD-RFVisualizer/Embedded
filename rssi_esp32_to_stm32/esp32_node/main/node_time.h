#ifndef NODE_TIME_H
#define NODE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t node_time_sync_start(void);
uint64_t node_time_now_ms(void);
bool node_time_is_valid(void);

#endif

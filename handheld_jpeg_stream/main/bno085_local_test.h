#pragma once

#include "esp_err.h"

esp_err_t bno085_local_test_start(void);
esp_err_t bno085_local_test_lcd_begin(void);
void bno085_local_test_lcd_end(void);

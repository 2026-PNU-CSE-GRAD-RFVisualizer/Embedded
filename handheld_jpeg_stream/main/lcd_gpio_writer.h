#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the panel with the verified GPIO sequence, then switches frame
 * transfers to the ESP32-S3 16-bit I80 DMA peripheral. */
esp_err_t lcd_gpio_writer_init(void);
void lcd_gpio_writer_fill(uint16_t color);
void lcd_gpio_writer_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                               uint16_t height, uint16_t color);
esp_err_t lcd_gpio_writer_draw(const uint16_t *pixels, size_t pixel_count);

#ifdef __cplusplus
}
#endif


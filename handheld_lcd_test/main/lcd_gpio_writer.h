#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Takes ownership of the LCD data/control pins after the ESP LCD I80 driver
 * has been deleted. This intentionally slow writer is used to verify the
 * physical 16-bit bus without DMA byte-packing ambiguity.
 */
esp_err_t lcd_gpio_writer_init(void);
void lcd_gpio_writer_fill(uint16_t color);
esp_err_t lcd_gpio_writer_draw(const uint16_t *pixels, size_t pixel_count);

#ifdef __cplusplus
}
#endif

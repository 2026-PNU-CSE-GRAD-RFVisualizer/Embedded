#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt35510_t *nt35510_handle_t;

esp_err_t nt35510_new(esp_lcd_panel_io_handle_t io, int reset_gpio,
                      nt35510_handle_t *out_lcd);

esp_err_t nt35510_draw_bitmap(nt35510_handle_t lcd,
                              uint16_t x_start, uint16_t y_start,
                              uint16_t x_end, uint16_t y_end,
                              const uint16_t *pixels);

/* Start and feed a full 480x800 frame in multiple RGB565 chunks. */
esp_err_t nt35510_begin_frame(nt35510_handle_t lcd);
esp_err_t nt35510_write_frame_chunk(nt35510_handle_t lcd,
                                    const uint16_t *pixels,
                                    size_t pixel_count);

void nt35510_delete(nt35510_handle_t lcd);

bool nt35510_color_transfer_done_callback(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *event_data,
    void *user_ctx);

#ifdef __cplusplus
}
#endif

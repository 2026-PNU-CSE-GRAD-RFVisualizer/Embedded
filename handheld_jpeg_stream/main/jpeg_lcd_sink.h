#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "jpeg_stream_client.h"

esp_err_t jpeg_lcd_sink_init(void);
esp_err_t jpeg_lcd_sink_render(const jpeg_stream_frame_t *frame);
esp_err_t jpeg_lcd_sink_decode_rgb332(const jpeg_stream_frame_t *frame,
                                      uint8_t *output, size_t output_size);
void jpeg_lcd_sink_release_decode_buffer(void);

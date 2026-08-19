#pragma once

#include "esp_err.h"
#include "jpeg_stream_client.h"

esp_err_t jpeg_lcd_sink_init(void);
esp_err_t jpeg_lcd_sink_render(const jpeg_stream_frame_t *frame);


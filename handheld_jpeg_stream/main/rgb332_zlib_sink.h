#pragma once

#include "esp_err.h"

#include "jpeg_stream_client.h"

esp_err_t rgb332_zlib_sink_render(const jpeg_stream_frame_t *frame);

#include "rgb332_zlib_sink.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "miniz.h"

#include "jpeg_stream_protocol.h"
#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

#define RGB332_FRAME_BYTES ((size_t)LCD_H_RES * LCD_V_RES)

static const char *TAG = "rgb332_zlib";
static uint8_t *s_rgb332_frame;
static int64_t s_fps_window_start_us;
static uint32_t s_fps_window_frames;

esp_err_t rgb332_zlib_sink_render(const jpeg_stream_frame_t *frame)
{
    ESP_RETURN_ON_FALSE(frame != NULL && frame->jpeg != NULL &&
                            frame->jpeg_length > 0,
                        ESP_ERR_INVALID_ARG, TAG, "empty compressed frame");
    ESP_RETURN_ON_FALSE(frame->flags == JPEG_STREAM_FLAG_RGB332_ZLIB,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "frame is not RGB332+zlib");

    if (s_rgb332_frame == NULL) {
        s_rgb332_frame = heap_caps_aligned_alloc(
            16, RGB332_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(s_rgb332_frame != NULL, ESP_ERR_NO_MEM, TAG,
                            "RGB332 PSRAM allocation failed");
    }

    const int64_t inflate_start_us = esp_timer_get_time();
    if (s_fps_window_frames == 0) {
        s_fps_window_start_us = inflate_start_us;
    }
    const size_t decoded_bytes = tinfl_decompress_mem_to_mem(
        s_rgb332_frame, RGB332_FRAME_BYTES, frame->jpeg, frame->jpeg_length,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    const int64_t inflate_elapsed_us =
        esp_timer_get_time() - inflate_start_us;

    if (decoded_bytes == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG,
                 "seq=%lu invalid zlib stream (%u B); payload must be "
                 "zlib.compress(RGB332 bytes)",
                 (unsigned long)frame->seq, (unsigned)frame->jpeg_length);
        return ESP_ERR_INVALID_CRC;
    }
    if (decoded_bytes != RGB332_FRAME_BYTES) {
        ESP_LOGE(TAG,
                 "seq=%lu decoded=%u B, expected=%u B; do not zlib the "
                 "JPEG/PNG file itself, convert to 800x480 RGB332 first",
                 (unsigned long)frame->seq, (unsigned)decoded_bytes,
                 (unsigned)RGB332_FRAME_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    const int64_t draw_start_us = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(
        lcd_gpio_writer_draw_rgb332(s_rgb332_frame, RGB332_FRAME_BYTES), TAG,
        "RGB332 DMA draw failed");
    const int64_t draw_elapsed_us = esp_timer_get_time() - draw_start_us;

    ESP_LOGI(TAG,
             "displayed seq=%lu, compressed=%u B, inflate=%lld ms, "
             "draw=%lld ms, total=%lld ms",
             (unsigned long)frame->seq, (unsigned)frame->jpeg_length,
             (long long)(inflate_elapsed_us / 1000),
             (long long)(draw_elapsed_us / 1000),
             (long long)((inflate_elapsed_us + draw_elapsed_us) / 1000));

    ++s_fps_window_frames;
    if (s_fps_window_frames == 10) {
        const int64_t window_us = esp_timer_get_time() - s_fps_window_start_us;
        ESP_LOGI(TAG, "10-frame window=%lld ms, displayed fps=%.2f",
                 (long long)(window_us / 1000),
                 10000000.0 / (double)window_us);
        s_fps_window_frames = 0;
    }
    return ESP_OK;
}

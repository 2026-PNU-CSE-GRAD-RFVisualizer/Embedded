#include "rgb332_zlib_sink.h"

#include <stdbool.h>
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

#define INDEX_FRAME_BYTES       ((size_t)LCD_H_RES * LCD_V_RES)
#define PALETTE256_ENTRY_COUNT  256U
#define PALETTE256_BYTES        (PALETTE256_ENTRY_COUNT * sizeof(uint16_t))
#define PALETTE256_FRAME_BYTES  (PALETTE256_BYTES + INDEX_FRAME_BYTES)

static const char *TAG = "indexed_zlib";
static uint8_t *s_indexed_frame;
static int64_t s_fps_window_start_us;
static uint32_t s_fps_window_frames;

esp_err_t rgb332_zlib_sink_render(const jpeg_stream_frame_t *frame)
{
    ESP_RETURN_ON_FALSE(frame != NULL && frame->jpeg != NULL &&
                            frame->jpeg_length > 0,
                        ESP_ERR_INVALID_ARG, TAG, "empty compressed frame");
    const bool is_rgb332 = frame->flags == JPEG_STREAM_FLAG_RGB332_ZLIB;
    const bool is_palette256 =
        frame->flags == JPEG_STREAM_FLAG_PALETTE256_ZLIB;
    ESP_RETURN_ON_FALSE(is_rgb332 || is_palette256,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "frame is not an indexed zlib format");

    if (s_indexed_frame == NULL) {
        s_indexed_frame = heap_caps_aligned_alloc(
            16, PALETTE256_FRAME_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(s_indexed_frame != NULL, ESP_ERR_NO_MEM, TAG,
                            "indexed frame PSRAM allocation failed");
    }

    const size_t expected_bytes = is_palette256
        ? PALETTE256_FRAME_BYTES : INDEX_FRAME_BYTES;
    const char *format_name = is_palette256 ? "palette256" : "rgb332";

    const int64_t inflate_start_us = esp_timer_get_time();
    if (s_fps_window_frames == 0) {
        s_fps_window_start_us = inflate_start_us;
    }
    const size_t decoded_bytes = tinfl_decompress_mem_to_mem(
        s_indexed_frame, PALETTE256_FRAME_BYTES,
        frame->jpeg, frame->jpeg_length,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    const int64_t inflate_elapsed_us =
        esp_timer_get_time() - inflate_start_us;

    if (decoded_bytes == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG,
                 "seq=%lu format=%s invalid zlib stream (%u B)",
                 (unsigned long)frame->seq, format_name,
                 (unsigned)frame->jpeg_length);
        return ESP_ERR_INVALID_CRC;
    }
    if (decoded_bytes != expected_bytes) {
        ESP_LOGE(TAG,
                 "seq=%lu format=%s decoded=%u B, expected=%u B",
                 (unsigned long)frame->seq, format_name,
                 (unsigned)decoded_bytes,
                 (unsigned)expected_bytes);
        return ESP_ERR_INVALID_SIZE;
    }

    const int64_t draw_start_us = esp_timer_get_time();
    esp_err_t draw_result;
    if (is_palette256) {
        draw_result = lcd_gpio_writer_draw_palette256(
            s_indexed_frame + PALETTE256_BYTES, INDEX_FRAME_BYTES,
            s_indexed_frame, PALETTE256_BYTES);
    } else {
        draw_result = lcd_gpio_writer_draw_rgb332(
            s_indexed_frame, INDEX_FRAME_BYTES);
    }
    ESP_RETURN_ON_ERROR(draw_result, TAG, "indexed DMA draw failed");
    const int64_t draw_elapsed_us = esp_timer_get_time() - draw_start_us;

    ESP_LOGI(TAG,
             "displayed seq=%lu format=%s, compressed=%u B, inflate=%lld ms, "
             "draw=%lld ms, total=%lld ms",
             (unsigned long)frame->seq, format_name,
             (unsigned)frame->jpeg_length,
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

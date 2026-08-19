#include "local_animation_test.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jpeg_lcd_sink.h"
#include "jpeg_stream_client.h"
#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

#define TEST_FRAME_COUNT 10U
#define TEST_FRAME_PERIOD_MS 100U
#define RGB332_FRAME_BYTES ((size_t)LCD_H_RES * LCD_V_RES)
#define RGB332_CACHE_BYTES (RGB332_FRAME_BYTES * TEST_FRAME_COUNT)

static const char *TAG = "local_10fps";

#define DECLARE_FRAME(index)                                                   \
    extern const uint8_t frame_##index##_start[]                              \
        asm("_binary_frame_" #index "_jpg_start");                           \
    extern const uint8_t frame_##index##_end[]                                \
        asm("_binary_frame_" #index "_jpg_end")

DECLARE_FRAME(00);
DECLARE_FRAME(01);
DECLARE_FRAME(02);
DECLARE_FRAME(03);
DECLARE_FRAME(04);
DECLARE_FRAME(05);
DECLARE_FRAME(06);
DECLARE_FRAME(07);
DECLARE_FRAME(08);
DECLARE_FRAME(09);

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} embedded_jpeg_t;

static const embedded_jpeg_t TEST_FRAMES[TEST_FRAME_COUNT] = {
    {frame_00_start, frame_00_end},
    {frame_01_start, frame_01_end},
    {frame_02_start, frame_02_end},
    {frame_03_start, frame_03_end},
    {frame_04_start, frame_04_end},
    {frame_05_start, frame_05_end},
    {frame_06_start, frame_06_end},
    {frame_07_start, frame_07_end},
    {frame_08_start, frame_08_end},
    {frame_09_start, frame_09_end},
};

void local_animation_test_run(void)
{
    uint8_t *frame_cache = heap_caps_aligned_alloc(
        16, RGB332_CACHE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(frame_cache != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "preloading 10 JPEGs into %u-byte RGB332 PSRAM cache",
             (unsigned)RGB332_CACHE_BYTES);
    for (size_t index = 0; index < TEST_FRAME_COUNT; ++index) {
        const embedded_jpeg_t *embedded = &TEST_FRAMES[index];
        const jpeg_stream_frame_t frame = {
            .seq = (uint32_t)index,
            .timestamp_ms = 0,
            .flags = 0,
            .jpeg = embedded->start,
            .jpeg_length = (size_t)(embedded->end - embedded->start),
        };
        ESP_ERROR_CHECK(jpeg_lcd_sink_decode_rgb332(
            &frame, frame_cache + index * RGB332_FRAME_BYTES,
            RGB332_FRAME_BYTES));
    }
    jpeg_lcd_sink_release_decode_buffer();

    const TickType_t frame_period = pdMS_TO_TICKS(TEST_FRAME_PERIOD_MS);
    uint32_t loop_count = 0;

    ESP_LOGI(TAG, "cache ready; starting 10-frame loop: 800x480, 100 ms/frame");
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const int64_t loop_start_us = esp_timer_get_time();
        uint32_t slow_frames = 0;

        for (size_t index = 0; index < TEST_FRAME_COUNT; ++index) {
            const int64_t render_start_us = esp_timer_get_time();
            const esp_err_t result = lcd_gpio_writer_draw_rgb332(
                frame_cache + index * RGB332_FRAME_BYTES,
                RGB332_FRAME_BYTES);
            const int64_t render_us = esp_timer_get_time() - render_start_us;

            if (result != ESP_OK) {
                ESP_LOGE(TAG, "frame %u failed: %s", (unsigned)index,
                         esp_err_to_name(result));
            }
            if (render_us > (int64_t)TEST_FRAME_PERIOD_MS * 1000) {
                ++slow_frames;
                ESP_LOGW(TAG, "frame %u missed 100 ms budget: %lld ms",
                         (unsigned)index, (long long)(render_us / 1000));
            }

            /* Absolute scheduling prevents per-frame render jitter accumulating. */
            xTaskDelayUntil(&last_wake, frame_period);
        }

        ++loop_count;
        const int64_t elapsed_us = esp_timer_get_time() - loop_start_us;
        const double measured_fps =
            (double)TEST_FRAME_COUNT * 1000000.0 / (double)elapsed_us;
        ESP_LOGI(TAG, "loop=%lu elapsed=%lld ms fps=%.2f slow=%lu/10",
                 (unsigned long)loop_count, (long long)(elapsed_us / 1000),
                 measured_fps, (unsigned long)slow_frames);
    }
}

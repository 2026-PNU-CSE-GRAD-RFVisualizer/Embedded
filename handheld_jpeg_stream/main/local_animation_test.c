#include "local_animation_test.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "jpeg_lcd_sink.h"
#include "jpeg_stream_client.h"
#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"
#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
#include "bno085.h"
#endif

#define TEST_FRAME_COUNT 10U
#define TEST_FRAME_PERIOD_MS 100U
#define RGB332_FRAME_BYTES ((size_t)LCD_H_RES * LCD_V_RES)
#define RGB332_CACHE_BYTES (RGB332_FRAME_BYTES * TEST_FRAME_COUNT)
#define PI_F 3.14159265358979323846f

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

#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
static float quaternion_yaw(const bno085_quaternion_t *quaternion)
{
    const float sin_yaw = 2.0f *
        (quaternion->w * quaternion->z +
         quaternion->x * quaternion->y);
    const float cos_yaw = 1.0f - 2.0f *
        (quaternion->y * quaternion->y +
         quaternion->z * quaternion->z);
    return atan2f(sin_yaw, cos_yaw);
}

static float wrap_radians(float angle)
{
    while (angle > PI_F) {
        angle -= 2.0f * PI_F;
    }
    while (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}

static bool orientation_frame_index(size_t *frame_index,
                                    float *relative_yaw_degrees)
{
    static bool centered;
    static float center_yaw;
    bno085_quaternion_t quaternion;
    if (!bno085_get_latest_quaternion(&quaternion)) {
        return false;
    }

    const float yaw = quaternion_yaw(&quaternion);
    if (!centered) {
        center_yaw = yaw;
        centered = true;
        ESP_LOGI(TAG, "local view recentered at BNO seq=%lu",
                 (unsigned long)quaternion.sequence);
    }

    const float relative_yaw = wrap_radians(yaw - center_yaw);
    const float yaw_degrees = relative_yaw * (180.0f / PI_F);
    int selected = (int)(TEST_FRAME_COUNT / 2U) +
                   (int)lroundf(yaw_degrees / 15.0f);
    if (selected < 0) {
        selected = 0;
    } else if (selected >= (int)TEST_FRAME_COUNT) {
        selected = (int)TEST_FRAME_COUNT - 1;
    }

    *frame_index = (size_t)selected;
    *relative_yaw_degrees = yaw_degrees;
    return true;
}
#endif

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
#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
    float view_yaw_degrees = 0.0f;
    size_t view_frame_index = TEST_FRAME_COUNT / 2U;
    ESP_LOGI(TAG, "BNO-controlled local view enabled: yaw selects frame 0..9");
#endif

    ESP_LOGI(TAG, "cache ready; starting 10-frame loop: 800x480, 100 ms/frame");
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const int64_t loop_start_us = esp_timer_get_time();
        uint32_t slow_frames = 0;
        int64_t total_render_us = 0;
        int64_t max_render_us = 0;

        for (size_t index = 0; index < TEST_FRAME_COUNT; ++index) {
            size_t display_index = index;
#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
            if (orientation_frame_index(&view_frame_index,
                                        &view_yaw_degrees)) {
                display_index = view_frame_index;
            }
#endif
            const int64_t render_start_us = esp_timer_get_time();
            const esp_err_t result = lcd_gpio_writer_draw_rgb332(
                frame_cache + display_index * RGB332_FRAME_BYTES,
                RGB332_FRAME_BYTES);
            const int64_t render_us = esp_timer_get_time() - render_start_us;
            total_render_us += render_us;
            if (render_us > max_render_us) {
                max_render_us = render_us;
            }

            if (result != ESP_OK) {
                ESP_LOGE(TAG, "frame %u failed: %s", (unsigned)index,
                         esp_err_to_name(result));
                if (result == ESP_ERR_TIMEOUT) {
                    ESP_LOGE(TAG,
                             "LCD DMA stalled; restarting to recover the bus");
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                }
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
        ESP_LOGI(TAG,
                 "loop=%lu elapsed=%lld ms fps=%.2f slow=%lu/10 "
                 "render_avg=%.1fms render_max=%.1fms",
                 (unsigned long)loop_count, (long long)(elapsed_us / 1000),
                 measured_fps, (unsigned long)slow_frames,
                 (double)total_render_us / (TEST_FRAME_COUNT * 1000.0),
                 (double)max_render_us / 1000.0);
#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
        ESP_LOGI(TAG, "local view: relative_yaw=%.1f deg frame=%u",
                 view_yaw_degrees, (unsigned)view_frame_index);
#endif
    }
}

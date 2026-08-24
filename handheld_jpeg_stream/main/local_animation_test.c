#include "local_animation_test.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

#if CONFIG_HANDHELD_LOCAL_VIEWPORT_TEST
#define VIEW_WORLD_WIDTH 2400U
#define VIEW_WORLD_HEIGHT 960U
#define VIEW_WORLD_BYTES ((size_t)VIEW_WORLD_WIDTH * VIEW_WORLD_HEIGHT)
#define VIEW_PITCH_LIMIT_DEGREES 40.0f
#endif

static const char *TAG = "local_10fps";

#if !CONFIG_HANDHELD_LOCAL_VIEWPORT_TEST
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
#endif

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

static float quaternion_pitch(const bno085_quaternion_t *quaternion)
{
    float sin_pitch = 2.0f *
        (quaternion->w * quaternion->y -
         quaternion->z * quaternion->x);
    if (sin_pitch > 1.0f) {
        sin_pitch = 1.0f;
    } else if (sin_pitch < -1.0f) {
        sin_pitch = -1.0f;
    }
    return asinf(sin_pitch);
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

#if !CONFIG_HANDHELD_LOCAL_VIEWPORT_TEST
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
#endif

#if CONFIG_HANDHELD_LOCAL_VIEWPORT_TEST
static uint8_t rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint8_t)((red & 0xe0U) | ((green >> 3) & 0x1cU) | (blue >> 6));
}

static void build_test_world(uint8_t *world)
{
    static const uint8_t sector_tint[8][3] = {
        {80, 20, 20}, {80, 55, 10}, {55, 75, 10}, {15, 75, 30},
        {10, 65, 75}, {15, 30, 85}, {55, 20, 85}, {85, 20, 55},
    };

    for (uint32_t y = 0; y < VIEW_WORLD_HEIGHT; ++y) {
        for (uint32_t x = 0; x < VIEW_WORLD_WIDTH; ++x) {
            const uint32_t sector = (x * 8U) / VIEW_WORLD_WIDTH;
            const uint8_t *tint = sector_tint[sector];
            uint8_t red;
            uint8_t green;
            uint8_t blue;

            if (y < VIEW_WORLD_HEIGHT / 2U) {
                const uint32_t shade = 30U +
                    (y * 100U) / (VIEW_WORLD_HEIGHT / 2U);
                red = (uint8_t)(shade + tint[0] / 3U);
                green = (uint8_t)(shade + tint[1] / 3U);
                blue = (uint8_t)(110U + shade / 2U + tint[2] / 4U);
            } else {
                const uint32_t depth = y - VIEW_WORLD_HEIGHT / 2U;
                red = (uint8_t)(25U + tint[0] / 2U + depth / 12U);
                green = (uint8_t)(55U + tint[1] / 2U + depth / 9U);
                blue = (uint8_t)(20U + tint[2] / 3U + depth / 16U);
            }

            /* Strong longitude/latitude guides make direction obvious. */
            if ((x % 300U) < 10U || (y % 120U) < 6U) {
                red = 235U;
                green = 235U;
                blue = 235U;
            }

            /* A distinct landmark in the center of every 45-degree sector. */
            const uint32_t local_x = x % 300U;
            const int32_t dy = (int32_t)y - (int32_t)(VIEW_WORLD_HEIGHT / 2U);
            if (local_x >= 125U && local_x < 175U && dy > -110 && dy < 110) {
                red = (uint8_t)(220U - sector * 18U);
                green = (uint8_t)(35U + sector * 24U);
                blue = (uint8_t)(50U + sector * 19U);
            }

            world[(size_t)y * VIEW_WORLD_WIDTH + x] = rgb332(red, green, blue);
        }
        if ((y & 31U) == 31U) {
            /* Keep ImuTask serviced while the panorama is generated once. */
            vTaskDelay(1);
        }
    }
}

static void copy_viewport(const uint8_t *world, uint8_t *viewport,
                          int32_t source_x, int32_t source_y)
{
    while (source_x < 0) {
        source_x += (int32_t)VIEW_WORLD_WIDTH;
    }
    source_x %= (int32_t)VIEW_WORLD_WIDTH;

    if (source_y < 0) {
        source_y = 0;
    }
    const int32_t max_y = (int32_t)VIEW_WORLD_HEIGHT - LCD_V_RES;
    if (source_y > max_y) {
        source_y = max_y;
    }

    const size_t first_width =
        source_x + LCD_H_RES <= VIEW_WORLD_WIDTH
            ? LCD_H_RES
            : VIEW_WORLD_WIDTH - (size_t)source_x;
    const size_t second_width = LCD_H_RES - first_width;

    for (size_t row = 0; row < LCD_V_RES; ++row) {
        const uint8_t *source = world +
            ((size_t)source_y + row) * VIEW_WORLD_WIDTH + source_x;
        uint8_t *destination = viewport + row * LCD_H_RES;
        memcpy(destination, source, first_width);
        if (second_width != 0U) {
            memcpy(destination + first_width,
                   world + ((size_t)source_y + row) * VIEW_WORLD_WIDTH,
                   second_width);
        }
    }

    /* Fixed crosshair: the world moves, while the user's gaze stays centered. */
    const size_t center_x = LCD_H_RES / 2U;
    const size_t center_y = LCD_V_RES / 2U;
    for (size_t offset = 0; offset <= 18U; ++offset) {
        const uint8_t color = (offset & 1U) ? rgb332(0, 0, 0)
                                             : rgb332(255, 255, 255);
        viewport[center_y * LCD_H_RES + center_x - offset] = color;
        viewport[center_y * LCD_H_RES + center_x + offset] = color;
        viewport[(center_y - offset) * LCD_H_RES + center_x] = color;
        viewport[(center_y + offset) * LCD_H_RES + center_x] = color;
    }
}

static void local_viewport_test_run(void) __attribute__((noreturn));

static void local_viewport_test_run(void)
{
    uint8_t *world = heap_caps_aligned_alloc(
        16, VIEW_WORLD_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *viewport = heap_caps_aligned_alloc(
        16, RGB332_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(world != NULL && viewport != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "building offline %ux%u panorama in PSRAM",
             VIEW_WORLD_WIDTH, VIEW_WORLD_HEIGHT);
    build_test_world(world);

    bool centered = false;
    float center_yaw = 0.0f;
    float center_pitch = 0.0f;
    float relative_yaw_degrees = 0.0f;
    float relative_pitch_degrees = 0.0f;
    uint32_t frame_count = 0;
    uint32_t slow_frames = 0;
    int64_t stats_start_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(TEST_FRAME_PERIOD_MS);

    ESP_LOGI(TAG, "offline viewport ready: yaw=horizontal pitch=vertical");
    for (;;) {
        const int64_t frame_start_us = esp_timer_get_time();
        bno085_quaternion_t quaternion;
        if (bno085_get_latest_quaternion(&quaternion)) {
            const float yaw = quaternion_yaw(&quaternion);
            const float pitch = quaternion_pitch(&quaternion);
            if (!centered) {
                center_yaw = yaw;
                center_pitch = pitch;
                centered = true;
                ESP_LOGI(TAG, "viewport recentered at BNO seq=%lu",
                         (unsigned long)quaternion.sequence);
            }
            relative_yaw_degrees =
                wrap_radians(yaw - center_yaw) * (180.0f / PI_F);
            relative_pitch_degrees =
                (pitch - center_pitch) * (180.0f / PI_F);
        }

        if (relative_pitch_degrees > VIEW_PITCH_LIMIT_DEGREES) {
            relative_pitch_degrees = VIEW_PITCH_LIMIT_DEGREES;
        } else if (relative_pitch_degrees < -VIEW_PITCH_LIMIT_DEGREES) {
            relative_pitch_degrees = -VIEW_PITCH_LIMIT_DEGREES;
        }

        const int32_t center_x = (int32_t)(VIEW_WORLD_WIDTH / 2U) +
            (int32_t)lroundf(relative_yaw_degrees *
                ((float)VIEW_WORLD_WIDTH / 360.0f));
        const int32_t center_y = (int32_t)(VIEW_WORLD_HEIGHT / 2U) -
            (int32_t)lroundf(relative_pitch_degrees *
                (((float)VIEW_WORLD_HEIGHT - LCD_V_RES) /
                 (2.0f * VIEW_PITCH_LIMIT_DEGREES)));
        copy_viewport(world, viewport,
                      center_x - (int32_t)(LCD_H_RES / 2U),
                      center_y - (int32_t)(LCD_V_RES / 2U));

        const esp_err_t result =
            lcd_gpio_writer_draw_rgb332(viewport, RGB332_FRAME_BYTES);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "viewport draw failed: %s", esp_err_to_name(result));
            if (result == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        const int64_t frame_us = esp_timer_get_time() - frame_start_us;
        if (frame_us > (int64_t)TEST_FRAME_PERIOD_MS * 1000) {
            ++slow_frames;
        }
        xTaskDelayUntil(&last_wake, frame_period);
        ++frame_count;
        if (frame_count == TEST_FRAME_COUNT) {
            const int64_t elapsed_us = esp_timer_get_time() - stats_start_us;
            ESP_LOGI(TAG,
                     "viewport fps=%.2f slow=%lu/10 yaw=%+.1f pitch=%+.1f",
                     (double)TEST_FRAME_COUNT * 1000000.0 /
                         (double)elapsed_us,
                     (unsigned long)slow_frames, relative_yaw_degrees,
                     relative_pitch_degrees);
            frame_count = 0;
            slow_frames = 0;
            stats_start_us = esp_timer_get_time();
        }
    }
}
#endif

void local_animation_test_run(void)
{
#if CONFIG_HANDHELD_LOCAL_VIEWPORT_TEST
    local_viewport_test_run();
#else
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
#endif
}

#include "jpeg_lcd_sink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "jpeg_decoder.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

static const char *TAG = "jpeg_lcd";
static uint16_t *s_rgb565_frame;
static bool s_initialized;

static const size_t RGB565_PIXEL_COUNT =
    (size_t)LCD_H_RES * (size_t)LCD_V_RES;
static const size_t RGB565_FRAME_BYTES =
    (size_t)LCD_H_RES * (size_t)LCD_V_RES * sizeof(uint16_t);

esp_err_t jpeg_lcd_sink_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t backlight_config = {
        .pin_bit_mask = (1ULL << LCD_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), TAG,
                        "backlight GPIO config failed");
    gpio_set_level(LCD_PIN_BL, !LCD_BL_ON_LEVEL);

    s_rgb565_frame = heap_caps_aligned_alloc(
        16, RGB565_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_rgb565_frame != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to allocate %u-byte RGB565 PSRAM frame",
                        (unsigned)RGB565_FRAME_BYTES);

    ESP_RETURN_ON_ERROR(lcd_gpio_writer_init(), TAG,
                        "NT35510 initialization failed");
    lcd_gpio_writer_fill(0x0000);
    gpio_set_level(LCD_PIN_BL, LCD_BL_ON_LEVEL);
    s_initialized = true;

    ESP_LOGI(TAG, "LCD ready: landscape=%ux%u, "
             "RGB565 buffer=%u bytes in PSRAM",
             LCD_H_RES, LCD_V_RES, (unsigned)RGB565_FRAME_BYTES);
    return ESP_OK;
}

esp_err_t jpeg_lcd_sink_render(const jpeg_stream_frame_t *frame)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "LCD sink is not initialized");
    ESP_RETURN_ON_FALSE(frame != NULL && frame->jpeg != NULL &&
                        frame->jpeg_length > 0,
                        ESP_ERR_INVALID_ARG, TAG, "empty JPEG frame");

    esp_jpeg_image_cfg_t info_config = {
        .indata = (uint8_t *)frame->jpeg,
        .indata_size = frame->jpeg_length,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            /* lcd_gpio_writer_draw() reads native uint16_t RGB565 values. */
            .swap_color_bytes = 0,
        },
    };
    esp_jpeg_image_output_t image_info = {0};
    esp_err_t result = esp_jpeg_get_image_info(&info_config, &image_info);
    ESP_RETURN_ON_ERROR(result, TAG, "seq=%lu JPEG header decode failed",
                        (unsigned long)frame->seq);

    ESP_RETURN_ON_FALSE(image_info.width == LCD_H_RES &&
                        image_info.height == LCD_V_RES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "seq=%lu resolution %ux%u; expected %ux%u",
                        (unsigned long)frame->seq,
                        image_info.width, image_info.height,
                        LCD_H_RES, LCD_V_RES);
    ESP_RETURN_ON_FALSE(image_info.output_len <= RGB565_FRAME_BYTES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "seq=%lu decoded size %u exceeds %u",
                        (unsigned long)frame->seq,
                        (unsigned)image_info.output_len,
                        (unsigned)RGB565_FRAME_BYTES);

    esp_jpeg_image_cfg_t decode_config = {
        .indata = (uint8_t *)frame->jpeg,
        .indata_size = frame->jpeg_length,
        .outbuf = (uint8_t *)s_rgb565_frame,
        .outbuf_size = RGB565_FRAME_BYTES,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 0,
        },
    };
    esp_jpeg_image_output_t decoded = {0};
    const int64_t decode_start_us = esp_timer_get_time();
    result = esp_jpeg_decode(&decode_config, &decoded);
    const int64_t decode_elapsed_us = esp_timer_get_time() - decode_start_us;
    ESP_RETURN_ON_ERROR(result, TAG,
                        "seq=%lu JPEG decode failed (progressive JPEG unsupported)",
                        (unsigned long)frame->seq);

    ESP_RETURN_ON_FALSE(decoded.width == LCD_H_RES &&
                        decoded.height == LCD_V_RES &&
                        decoded.output_len == RGB565_FRAME_BYTES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "seq=%lu decoder output mismatch: %ux%u, %u bytes",
                        (unsigned long)frame->seq,
                        decoded.width, decoded.height,
                        (unsigned)decoded.output_len);

    const int64_t draw_start_us = esp_timer_get_time();
    result = lcd_gpio_writer_draw(s_rgb565_frame, RGB565_PIXEL_COUNT);
    const int64_t draw_elapsed_us = esp_timer_get_time() - draw_start_us;
    ESP_RETURN_ON_ERROR(result, TAG, "seq=%lu LCD draw failed",
                        (unsigned long)frame->seq);

    ESP_LOGI(TAG, "displayed seq=%lu, jpeg=%u B, decode=%lld ms, draw=%lld ms",
             (unsigned long)frame->seq,
             (unsigned)frame->jpeg_length,
             (long long)(decode_elapsed_us / 1000),
             (long long)(draw_elapsed_us / 1000));
    return ESP_OK;
}

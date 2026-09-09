#include "jpeg_lcd_sink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#if CONFIG_HANDHELD_NEW_JPEG
#include "esp_jpeg_dec.h"
#else
#include "jpeg_decoder.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

static const char *TAG = "jpeg_lcd";
static uint16_t *s_rgb565_frame;
#if !CONFIG_HANDHELD_NEW_JPEG
static uint8_t *s_jpeg_work_buffer;
#endif
static bool s_initialized;

#define JPEG_WORK_BUFFER_BYTES (16U * 1024U)

static const size_t RGB565_PIXEL_COUNT =
    (size_t)LCD_H_RES * (size_t)LCD_V_RES;
static const size_t RGB565_FRAME_BYTES =
    (size_t)LCD_H_RES * (size_t)LCD_V_RES * sizeof(uint16_t);

static void upscale_rgb565_in_place(uint16_t source_width,
                                    uint16_t source_height)
{
    /* Expand from the end so destination pixels cannot overwrite source
     * pixels that have not been consumed yet. */
    for (size_t y = LCD_V_RES; y-- > 0;) {
        const size_t source_y = y * source_height / LCD_V_RES;
        for (size_t x = LCD_H_RES; x-- > 0;) {
            const size_t source_x = x * source_width / LCD_H_RES;
            s_rgb565_frame[y * LCD_H_RES + x] =
                s_rgb565_frame[source_y * source_width + source_x];
        }
    }
}

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

#if !CONFIG_HANDHELD_NEW_JPEG
    /* esp_jpeg's automatic 3.1 kB scratch allocation is too small for the
     * external decoder configured for RGB565 and some optimized Huffman
     * tables. Keep one internal-RAM work buffer and reuse it every frame. */
    s_jpeg_work_buffer = heap_caps_aligned_alloc(
        16, JPEG_WORK_BUFFER_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_jpeg_work_buffer != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to allocate %u-byte JPEG work buffer",
                        (unsigned)JPEG_WORK_BUFFER_BYTES);

#endif

    ESP_RETURN_ON_ERROR(lcd_gpio_writer_init(), TAG,
                        "NT35510 initialization failed");
    lcd_gpio_writer_fill(0x0000);
    gpio_set_level(LCD_PIN_BL, LCD_BL_ON_LEVEL);
    s_initialized = true;

#if CONFIG_HANDHELD_NEW_JPEG
    const char *decoder_name = "esp_new_jpeg 1.0.2";
#else
    const char *decoder_name = "esp_jpeg 1.3.1";
#endif
    ESP_LOGI(TAG, "LCD ready: landscape=%ux%u, "
             "RGB565 buffer=%u bytes in PSRAM, decoder=%s",
             LCD_H_RES, LCD_V_RES, (unsigned)RGB565_FRAME_BYTES, decoder_name);
    return ESP_OK;
}

static esp_err_t decode_rgb565(const jpeg_stream_frame_t *frame,
                               int64_t *decode_elapsed_us)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "LCD sink is not initialized");
    ESP_RETURN_ON_FALSE(s_rgb565_frame != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "JPEG decode buffer was released");
    ESP_RETURN_ON_FALSE(frame != NULL && frame->jpeg != NULL &&
                        frame->jpeg_length > 0,
                        ESP_ERR_INVALID_ARG, TAG, "empty JPEG frame");

#if CONFIG_HANDHELD_NEW_JPEG
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = NULL;
    const int64_t start_us = esp_timer_get_time();
    jpeg_error_t status = jpeg_dec_open(&config, &decoder);
    ESP_RETURN_ON_FALSE(status == JPEG_ERR_OK, ESP_FAIL, TAG,
                        "JPEG open failed: %d", status);
    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)frame->jpeg,
        .inbuf_len = (int)frame->jpeg_length,
        .outbuf = (uint8_t *)s_rgb565_frame,
    };
    jpeg_dec_header_info_t header = {0};
    esp_err_t result = ESP_FAIL;
    status = jpeg_dec_parse_header(decoder, &io, &header);
    if (status != JPEG_ERR_OK) {
        goto decode_done;
    }
    int required = 0;
    status = jpeg_dec_get_outbuf_len(decoder, &required);
    if (status != JPEG_ERR_OK || required <= 0 ||
        (size_t)required > RGB565_FRAME_BYTES ||
        header.width == 0 || header.height == 0 ||
        header.width > LCD_H_RES || header.height > LCD_V_RES) {
        result = ESP_ERR_INVALID_SIZE;
        goto decode_done;
    }
    status = jpeg_dec_process(decoder, &io);
    if (status == JPEG_ERR_OK) {
        if (io.out_size != (int)((size_t)header.width * header.height * 2U)) {
            result = ESP_ERR_INVALID_SIZE;
            goto decode_done;
        }
        if (header.width != LCD_H_RES || header.height != LCD_V_RES) {
            upscale_rgb565_in_place(header.width, header.height);
        }
        result = ESP_OK;
    }
decode_done:
    jpeg_dec_close(decoder);
    *decode_elapsed_us = esp_timer_get_time() - start_us;
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "seq=%lu JPEG failed: codec=%d result=%s",
                 (unsigned long)frame->seq, status, esp_err_to_name(result));
    }
    return result;
#else
    const int64_t decode_start_us = esp_timer_get_time();
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

    ESP_RETURN_ON_FALSE(image_info.width > 0 && image_info.height > 0 &&
                        image_info.width <= LCD_H_RES &&
                        image_info.height <= LCD_V_RES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "seq=%lu resolution %ux%u exceeds LCD %ux%u",
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
        .advanced = {
            .working_buffer = s_jpeg_work_buffer,
            .working_buffer_size = JPEG_WORK_BUFFER_BYTES,
        },
    };
    esp_jpeg_image_output_t decoded = {0};
    result = esp_jpeg_decode(&decode_config, &decoded);
    ESP_RETURN_ON_ERROR(result, TAG,
                        "seq=%lu JPEG decode failed",
                        (unsigned long)frame->seq);

    const size_t decoded_bytes =
        (size_t)decoded.width * decoded.height * sizeof(uint16_t);
    ESP_RETURN_ON_FALSE(decoded.width == image_info.width &&
                        decoded.height == image_info.height &&
                        decoded.output_len == decoded_bytes,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "seq=%lu decoder output mismatch: %ux%u, %u bytes",
                        (unsigned long)frame->seq,
                        decoded.width, decoded.height,
                        (unsigned)decoded.output_len);

    if (decoded.width != LCD_H_RES || decoded.height != LCD_V_RES) {
        upscale_rgb565_in_place(decoded.width, decoded.height);
        ESP_LOGD(TAG, "seq=%lu scaled %ux%u -> %ux%u",
                 (unsigned long)frame->seq,
                 decoded.width, decoded.height, LCD_H_RES, LCD_V_RES);
    }

    *decode_elapsed_us = esp_timer_get_time() - decode_start_us;
    return ESP_OK;
#endif
}

esp_err_t jpeg_lcd_sink_render(const jpeg_stream_frame_t *frame)
{
    int64_t decode_elapsed_us = 0;
    ESP_RETURN_ON_ERROR(decode_rgb565(frame, &decode_elapsed_us), TAG,
                        "JPEG decode failed");

    const int64_t draw_start_us = esp_timer_get_time();
    const esp_err_t result =
        lcd_gpio_writer_draw(s_rgb565_frame, RGB565_PIXEL_COUNT);
    const int64_t draw_elapsed_us = esp_timer_get_time() - draw_start_us;
    ESP_RETURN_ON_ERROR(result, TAG, "seq=%lu LCD draw failed",
                        (unsigned long)frame->seq);

    static int64_t window_start, decode_sum, decode_max, draw_sum, draw_max;
    static unsigned count;
    if (!window_start) window_start = draw_start_us - decode_elapsed_us;
    decode_sum += decode_elapsed_us;
    draw_sum += draw_elapsed_us;
    if (decode_elapsed_us > decode_max) decode_max = decode_elapsed_us;
    if (draw_elapsed_us > draw_max) draw_max = draw_elapsed_us;
    ++count;
    const int64_t now = esp_timer_get_time();
    if (now - window_start >= 1000000) {
        ESP_LOGI(TAG, "JPEG decode_avg/max=%.2f/%.2f ms draw_avg/max=%.2f/%.2f ms n=%u",
                 decode_sum / (count * 1000.0), decode_max / 1000.0,
                 draw_sum / (count * 1000.0), draw_max / 1000.0, count);
        window_start = now;
        count = 0;
        decode_sum = decode_max = draw_sum = draw_max = 0;
    }
    ESP_LOGD(TAG, "displayed seq=%lu, jpeg=%u B, decode=%lld ms, draw=%lld ms",
             (unsigned long)frame->seq,
             (unsigned)frame->jpeg_length,
             (long long)(decode_elapsed_us / 1000),
             (long long)(draw_elapsed_us / 1000));
    return ESP_OK;
}

esp_err_t jpeg_lcd_sink_decode_rgb332(const jpeg_stream_frame_t *frame,
                                      uint8_t *output, size_t output_size)
{
    ESP_RETURN_ON_FALSE(output != NULL && output_size >= RGB565_PIXEL_COUNT,
                        ESP_ERR_INVALID_ARG, TAG,
                        "RGB332 output buffer is too small");

    int64_t decode_elapsed_us = 0;
    ESP_RETURN_ON_ERROR(decode_rgb565(frame, &decode_elapsed_us), TAG,
                        "JPEG cache decode failed");

    for (size_t i = 0; i < RGB565_PIXEL_COUNT; ++i) {
        const uint16_t color = s_rgb565_frame[i];
        output[i] = (uint8_t)(((color >> 8) & 0xE0) |
                              ((color >> 6) & 0x1C) |
                              ((color >> 3) & 0x03));
    }

    ESP_LOGI(TAG, "cached seq=%lu as RGB332, decode=%lld ms",
             (unsigned long)frame->seq,
             (long long)(decode_elapsed_us / 1000));
    return ESP_OK;
}

void jpeg_lcd_sink_release_decode_buffer(void)
{
    if (s_rgb565_frame != NULL) {
        heap_caps_free(s_rgb565_frame);
        s_rgb565_frame = NULL;
        ESP_LOGI(TAG, "released RGB565 JPEG decode buffer after caching");
    }
}

#if CONFIG_HANDHELD_RGB565_BENCHMARK
void jpeg_lcd_sink_benchmark(void)
{
    extern const uint8_t start[] asm("_binary_frame_00_jpg_start");
    extern const uint8_t end[] asm("_binary_frame_00_jpg_end");
    const jpeg_stream_frame_t frame = {.jpeg = start, .jpeg_length = end - start};
    int64_t decode_us;
    ESP_ERROR_CHECK(decode_rgb565(&frame, &decode_us));
    ESP_LOGI(TAG, "RGB565 LCD-only benchmark: decoded once, no pacing");
    for (;;) {
        ESP_ERROR_CHECK(lcd_gpio_writer_draw(s_rgb565_frame, RGB565_PIXEL_COUNT));
        taskYIELD();
    }
}
#endif

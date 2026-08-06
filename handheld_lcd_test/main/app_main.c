#include <stdint.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

static const char *TAG = "lcd_test";

extern const uint8_t testimage_start[]
    asm("_binary_testimage_rgb565_start");

extern const uint8_t testimage_end[]
    asm("_binary_testimage_rgb565_end");


static inline uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint16_t)(red & 0xF8) << 8) |
           ((uint16_t)(green & 0xFC) << 3) |
           ((uint16_t)blue >> 3);
}

void app_main(void)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = (1ULL << LCD_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    gpio_set_level(LCD_PIN_BL, !LCD_BL_ON_LEVEL);
    ESP_ERROR_CHECK(lcd_gpio_writer_init());

    gpio_set_level(LCD_PIN_BL, LCD_BL_ON_LEVEL);


    ESP_LOGI(TAG, "fill full screen test image");
    const size_t frame_pixels =
        (size_t)LCD_H_RES * (size_t)LCD_V_RES;

    const size_t expected_bytes =
        frame_pixels * sizeof(uint16_t);

    const size_t image_bytes =
        (size_t)(testimage_end - testimage_start);

    ESP_LOGI(TAG,
            "image bytes=%u, expected=%u",
            (unsigned)image_bytes,
            (unsigned)expected_bytes);

    ESP_ERROR_CHECK(
        image_bytes == expected_bytes
            ? ESP_OK
            : ESP_ERR_INVALID_SIZE
    );

    ESP_ERROR_CHECK(
        lcd_gpio_writer_draw(
            (const uint16_t *)testimage_start,
            frame_pixels
        )
    );

    ESP_LOGI(TAG, "image draw complete");


    ESP_LOGI(TAG, "test complete");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

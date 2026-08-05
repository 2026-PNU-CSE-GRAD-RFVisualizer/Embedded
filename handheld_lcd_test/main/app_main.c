#include <stdint.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

static const char *TAG = "lcd_test";

extern const uint8_t testimage_start[] asm("_binary_testimage_rgb565_start");
extern const uint8_t testimage_end[] asm("_binary_testimage_rgb565_end");

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

    ESP_LOGI(TAG, "direct GPIO solid red test");
    lcd_gpio_writer_fill(rgb565(255, 0, 0));
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "direct GPIO solid green test");
    lcd_gpio_writer_fill(rgb565(0, 255, 0));
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "direct GPIO solid blue test");
    lcd_gpio_writer_fill(rgb565(0, 0, 255));
    vTaskDelay(pdMS_TO_TICKS(1000));

    const size_t frame_bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    const size_t image_bytes = (size_t)(testimage_end - testimage_start);
    ESP_ERROR_CHECK(image_bytes == frame_bytes ? ESP_OK : ESP_ERR_INVALID_SIZE);
    ESP_LOGI(TAG, "drawing embedded 480x800 RGB565 test image (%u bytes)",
             (unsigned)frame_bytes);
    ESP_ERROR_CHECK(lcd_gpio_writer_draw((const uint16_t *)testimage_start,
                                         LCD_H_RES * LCD_V_RES));
    ESP_LOGI(TAG, "direct GPIO embedded image complete");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

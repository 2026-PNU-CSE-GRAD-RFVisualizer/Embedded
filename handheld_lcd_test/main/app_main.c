#include <stdint.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

static const char *TAG = "lcd_test";

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

    ESP_LOGI(TAG, "writing one full-screen solid green frame");
    lcd_gpio_writer_fill(rgb565(0, 255, 0));
    ESP_LOGI(TAG, "writing red to logical x=720..799 (right edge)");
    lcd_gpio_writer_fill_rect(720, 0, 80, 480, rgb565(255, 0, 0));
    ESP_LOGI(TAG, "right-edge address test complete; holding frame");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

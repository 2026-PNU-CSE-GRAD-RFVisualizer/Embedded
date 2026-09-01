#include "handheld_buttons.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define BUTTON_DEBOUNCE_US ((int64_t)HANDHELD_BUTTON_DEBOUNCE_MS * 1000)

typedef struct {
    gpio_num_t gpio;
    bool raw_held;
    bool debounced_held;
    int64_t raw_changed_us;
} button_input_t;

static const char *TAG = "handheld_buttons";
static button_input_t s_teleport = {
    .gpio = HANDHELD_TELEPORT_BUTTON_GPIO,
};
static button_input_t s_height_cycle = {
    .gpio = HANDHELD_HEIGHT_CYCLE_BUTTON_GPIO,
};

static bool read_active_low(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static void initialize_input(button_input_t *input, int64_t now_us)
{
    const bool held = read_active_low(input->gpio);
    input->raw_held = held;
    input->debounced_held = held;
    input->raw_changed_us = now_us;
}

static bool sample_input(button_input_t *input, int64_t now_us)
{
    const bool raw_held = read_active_low(input->gpio);
    if (raw_held != input->raw_held) {
        input->raw_held = raw_held;
        input->raw_changed_us = now_us;
    } else if (raw_held != input->debounced_held &&
               now_us - input->raw_changed_us >= BUTTON_DEBOUNCE_US) {
        input->debounced_held = raw_held;
    }
    return input->debounced_held;
}

esp_err_t handheld_buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (UINT64_C(1) << HANDHELD_TELEPORT_BUTTON_GPIO) |
                        (UINT64_C(1) << HANDHELD_HEIGHT_CYCLE_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    const int64_t now_us = esp_timer_get_time();
    initialize_input(&s_teleport, now_us);
    initialize_input(&s_height_cycle, now_us);
    ESP_LOGI(TAG,
             "active-low buttons: teleport=GPIO%d height_cycle=GPIO%d debounce=%u ms",
             HANDHELD_TELEPORT_BUTTON_GPIO,
             HANDHELD_HEIGHT_CYCLE_BUTTON_GPIO,
             (unsigned)HANDHELD_BUTTON_DEBOUNCE_MS);
    return ESP_OK;
}

handheld_button_state_t handheld_buttons_sample(void)
{
    const int64_t now_us = esp_timer_get_time();
    const handheld_button_state_t state = {
        .teleport_held = sample_input(&s_teleport, now_us),
        .height_cycle_held = sample_input(&s_height_cycle, now_us),
    };
    return state;
}

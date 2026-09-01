#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define HANDHELD_TELEPORT_BUTTON_GPIO 17
#define HANDHELD_HEIGHT_CYCLE_BUTTON_GPIO 19
#define HANDHELD_BUTTON_DEBOUNCE_MS 25U

typedef struct {
    bool teleport_held;
    bool height_cycle_held;
} handheld_button_state_t;

esp_err_t handheld_buttons_init(void);

/* Call regularly from ControlTxTask; this function does not create a task. */
handheld_button_state_t handheld_buttons_sample(void);

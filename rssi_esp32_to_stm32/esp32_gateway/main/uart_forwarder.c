#include "uart_forwarder.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#include "gateway_config.h"

typedef struct {
    char text[UART_LINE_MAX_LEN];
} uart_line_t;

static const char *TAG = "uart_forwarder";
static QueueHandle_t s_line_queue;

esp_err_t uart_forwarder_init(QueueHandle_t *out_line_queue)
{
    uart_config_t uart_config = {
        .baud_rate = STM32_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(STM32_UART_NUM, 1024, 0, 0, NULL, 0), TAG, "driver install");
    ESP_RETURN_ON_ERROR(uart_param_config(STM32_UART_NUM, &uart_config), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(STM32_UART_NUM,
                                     STM32_UART_TX_GPIO,
                                     STM32_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE),
                        TAG,
                        "uart pin");

    s_line_queue = xQueueCreate(UART_LINE_QUEUE_LEN, sizeof(uart_line_t));
    if (s_line_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *out_line_queue = s_line_queue;
    return ESP_OK;
}

void uart_forwarder_task(void *arg)
{
    (void)arg;
    uart_line_t line;
    while (true) {
        if (xQueueReceive(s_line_queue, &line, portMAX_DELAY) == pdTRUE) {
            size_t len = strlen(line.text);
            if (len > 0) {
                uart_write_bytes(STM32_UART_NUM, line.text, len);
                ESP_LOGI(TAG, "%s", line.text);
            }
        }
    }
}

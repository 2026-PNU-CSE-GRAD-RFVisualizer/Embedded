#ifndef UART_FORWARDER_H
#define UART_FORWARDER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

esp_err_t uart_forwarder_init(QueueHandle_t *out_line_queue);
void uart_forwarder_task(void *arg);

#endif

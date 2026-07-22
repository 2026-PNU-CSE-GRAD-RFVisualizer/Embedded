#include "rssi_line_parser.h"
#include "rssi_preprocessor.h"
#include "mqtt_payload.h"
#include "uart_rx_ring.h"

/*
 * Integration example for STM32 HAL projects.
 *
 * Replace huart1 with the UART connected to the ESP32 gateway.
 * This file is intentionally not a full CubeMX project.
 */

#ifdef STM32_HAL_AVAILABLE
#include "main.h"

extern UART_HandleTypeDef huart1;

static uint8_t s_uart_rx_byte;
static uart_rx_ring_t s_uart_rx_ring;
static rssi_preprocessor_t s_rssi_ctx;
static char s_mqtt_payload[MQTT_PAYLOAD_MAX_LEN];

void RssiReceiver_Init(void)
{
    rssi_parser_init();
    rssi_preprocessor_init(&s_rssi_ctx);
    uart_rx_ring_init(&s_uart_rx_ring);
    HAL_UART_Receive_IT(&huart1, &s_uart_rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        /* HAL Callback에서도 파싱하지 않고 바이트만 저장해 ISR 시간을 제한한다. */
        (void)uart_rx_ring_push_isr(&s_uart_rx_ring, s_uart_rx_byte);
        HAL_UART_Receive_IT(&huart1, &s_uart_rx_byte, 1);
    }
}

/* Call this frequently from the main loop or a low-priority task. */
void RssiReceiver_Process(void)
{
    /* Main Loop 또는 전용 Task에서 호출해 Parser와 상태 테이블을 한 Context에서 다룬다. */
    uint8_t byte;
    while (uart_rx_ring_pop(&s_uart_rx_ring, &byte)) {
        rssi_measurement_t measurement;
        rssi_parse_result_t result = rssi_parser_feed_byte(byte, &measurement);

        if (result == RSSI_PARSE_OK) {
            (void)rssi_preprocessor_update(&s_rssi_ctx, &measurement, HAL_GetTick());
        } else if (result == RSSI_PARSE_CHECKSUM_ERROR) {
            s_rssi_ctx.checksum_error_count++;
        } else if (result == RSSI_PARSE_FORMAT_ERROR) {
            s_rssi_ctx.format_error_count++;
        }
    }

    s_rssi_ctx.uart_overflow_count =
        uart_rx_ring_overflow_count(&s_uart_rx_ring);
}

void RssiReceiver_Periodic_1s(void)
{
    RssiReceiver_Process();
    uint32_t now_ms = HAL_GetTick();
    rssi_preprocessor_update_timeouts(&s_rssi_ctx, now_ms);

    int len = mqtt_payload_build_snapshot(s_mqtt_payload,
                                          sizeof(s_mqtt_payload),
                                          "gw-01",
                                          &s_rssi_ctx,
                                          now_ms);
    if (len > 0) {
        /*
         * Publish s_mqtt_payload here using the selected MQTT transport.
         * For example: mqtt_publish("gateway/gw-01", s_mqtt_payload, len);
         */
    }
}
#endif

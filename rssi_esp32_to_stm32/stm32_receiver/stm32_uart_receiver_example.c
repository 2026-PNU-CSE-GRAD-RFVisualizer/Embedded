#include "rssi_line_parser.h"
#include "rssi_preprocessor.h"
#include "mqtt_payload.h"

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
static rssi_preprocessor_t s_rssi_ctx;
static char s_mqtt_payload[MQTT_PAYLOAD_MAX_LEN];

void RssiReceiver_Init(void)
{
    rssi_parser_init();
    rssi_preprocessor_init(&s_rssi_ctx);
    HAL_UART_Receive_IT(&huart1, &s_uart_rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        rssi_measurement_t measurement;
        rssi_parse_result_t result = rssi_parser_feed_byte(s_uart_rx_byte, &measurement);
        uint32_t now_ms = HAL_GetTick();

        if (result == RSSI_PARSE_OK) {
            (void)rssi_preprocessor_update(&s_rssi_ctx, &measurement, now_ms);
        } else if (result == RSSI_PARSE_CHECKSUM_ERROR) {
            s_rssi_ctx.checksum_error_count++;
        } else if (result == RSSI_PARSE_FORMAT_ERROR) {
            s_rssi_ctx.format_error_count++;
        }

        HAL_UART_Receive_IT(&huart1, &s_uart_rx_byte, 1);
    }
}

void RssiReceiver_Periodic_1s(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint64_t snapshot_timestamp_ms = 0;
    rssi_preprocessor_update_timeouts(&s_rssi_ctx, now_ms);

    /* Replace 0 with Unix epoch milliseconds from the board RTC, if available. */

    int len = mqtt_payload_build_snapshot(s_mqtt_payload,
                                          sizeof(s_mqtt_payload),
                                          "gw-01",
                                          &s_rssi_ctx,
                                          now_ms,
                                          snapshot_timestamp_ms);
    if (len > 0) {
        /*
         * Publish s_mqtt_payload here using the selected MQTT transport.
         * For example: mqtt_publish("gateway/gw-01", s_mqtt_payload, len);
         */
    }
}
#endif

#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#define ESPNOW_WIFI_CHANNEL         6
#define TARGET_AP_CHANNEL           ESPNOW_WIFI_CHANNEL
#define TARGET_AP_USE_BSSID         1
#define TARGET_AP_SSID              "바부바부쟝"
#define TARGET_AP_BSSID             {0xB0, 0x38, 0x6C, 0x52, 0xBA, 0xFE}

/* Keep the real password in an untracked local override; do not commit secrets. */
#if __has_include("time_credentials.h")
#include "time_credentials.h"
#endif
#ifndef GATEWAY_TIME_SYNC_ENABLE
#define GATEWAY_TIME_SYNC_ENABLE     1
#endif
#define GATEWAY_TIME_WIFI_SSID       TARGET_AP_SSID
#ifndef GATEWAY_TIME_WIFI_PASSWORD
#define GATEWAY_TIME_WIFI_PASSWORD   ""
#endif
#define GATEWAY_TIME_SNTP_SERVER     "pool.ntp.org"
#define GATEWAY_TIME_VALID_AFTER_MS  1704067200000ULL

#define STM32_UART_NUM              UART_NUM_1
#define STM32_UART_BAUDRATE         115200
#define STM32_UART_TX_GPIO          17
#define STM32_UART_RX_GPIO          18

#define MAX_NODES                   8
#define GATEWAY_NODE_TIMEOUT_MS     3000u
#define ESPNOW_RX_QUEUE_LEN         32
#define UART_LINE_QUEUE_LEN         32
#define UART_LINE_MAX_LEN           128
#define GATEWAY_STATUS_LINE_ENABLE  0

/*
 * Set to 1 when only one ESP32 is available.
 * The gateway will generate fake remote RSSI lines for node_id 1..4.
 * If GATEWAY_LOCAL_RSSI_ENABLE is also 1, node_id 5 is real local Wi-Fi RSSI.
 */
#define GATEWAY_FAKE_RSSI_TEST      0
#define GATEWAY_FAKE_NODE_COUNT     4
#define GATEWAY_FAKE_PERIOD_MS      1000

/*
 * Normal 5-board deployment:
 * - ESP32 node firmware on node_id 1..4
 * - ESP32 gateway firmware also measures local RSSI as node_id 5
 */
#define GATEWAY_LOCAL_RSSI_ENABLE   1
#define GATEWAY_LOCAL_NODE_ID       5
#define GATEWAY_LOCAL_RSSI_PERIOD_MS 1000
#define GATEWAY_RSSI_FILTER_WINDOW  5
#define GATEWAY_SCAN_ACTIVE_MIN_MS  40
#define GATEWAY_SCAN_ACTIVE_MAX_MS  80
#define GATEWAY_RSSI_VALID_MIN_DBM  (-110)
#define GATEWAY_RSSI_VALID_MAX_DBM  (-10)
#define GATEWAY_SCAN_DEBUG_LOG      1

#define RSSI_ERR_NONE               0x0000u
#define RSSI_ERR_AP_NOT_FOUND       0x0001u
#define RSSI_ERR_SCAN_FAILED        0x0002u
#define RSSI_ERR_FILTER_EMPTY       0x0004u
#define RSSI_ERR_ESPNOW_SEND_FAIL   0x0008u
#define RSSI_ERR_TIME_INVALID       0x0010u
#define RSSI_ERR_LOW_HEAP           0x0020u

#if TARGET_AP_CHANNEL != ESPNOW_WIFI_CHANNEL
#error "TARGET_AP_CHANNEL and ESPNOW_WIFI_CHANNEL must match"
#endif

#endif

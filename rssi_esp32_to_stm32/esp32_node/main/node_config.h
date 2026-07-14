#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdint.h>

#define NODE_ID                         2
#define ESPNOW_WIFI_CHANNEL             6
#define TARGET_AP_CHANNEL               ESPNOW_WIFI_CHANNEL
#define TARGET_AP_USE_BSSID             0
#define TARGET_AP_SSID                  "U1MU2_2F"
#define TARGET_AP_BSSID                 {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define GATEWAY_ESPNOW_MAC              {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

#define RSSI_SAMPLE_INTERVAL_MS         200
#define RSSI_PUBLISH_PERIOD_MS          1000
#define RSSI_FILTER_WINDOW              5
#define RSSI_SCAN_ACTIVE_MIN_MS         40
#define RSSI_SCAN_ACTIVE_MAX_MS         80
#define ESPNOW_SEND_TIMEOUT_MS          300
#define ESPNOW_TX_SLOT_MS               120

#define RSSI_VALID_MIN_DBM              (-100)
#define RSSI_VALID_MAX_DBM              (-10)
#define RSSI_SCAN_DEBUG_LOG             1

#define RSSI_ERR_NONE                   0x0000u
#define RSSI_ERR_AP_NOT_FOUND           0x0001u
#define RSSI_ERR_SCAN_FAILED            0x0002u
#define RSSI_ERR_FILTER_EMPTY           0x0004u
#define RSSI_ERR_ESPNOW_SEND_FAIL       0x0008u
#define RSSI_ERR_TIME_INVALID           0x0010u
#define RSSI_ERR_LOW_HEAP               0x0020u

#if TARGET_AP_CHANNEL != ESPNOW_WIFI_CHANNEL
#error "TARGET_AP_CHANNEL and ESPNOW_WIFI_CHANNEL must match"
#endif

#endif

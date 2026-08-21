#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdint.h>

#define NODE_ID                         4
#define ESPNOW_WIFI_CHANNEL             6
#define TARGET_AP_CHANNEL               ESPNOW_WIFI_CHANNEL
#define TARGET_AP_USE_BSSID             1
#define TARGET_AP_SSID                  "바부바부쟝"
#define TARGET_AP_BSSID                 {0xB0, 0x38, 0x6C, 0x52, 0xBA, 0xFE}
#define GATEWAY_ESPNOW_MAC              {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/*
 * Unix epoch time is synchronized over the same AP used for RSSI measurement.
 * Keep the real password in an untracked local override; do not commit secrets.
 */
#if __has_include("time_credentials.h")
#include "time_credentials.h"
#endif
#ifndef NODE_TIME_SYNC_ENABLE
#define NODE_TIME_SYNC_ENABLE           1
#endif
#define NODE_TIME_WIFI_SSID             TARGET_AP_SSID
#ifndef NODE_TIME_WIFI_PASSWORD
#define NODE_TIME_WIFI_PASSWORD         ""
#endif
#define NODE_TIME_SNTP_SERVER           "pool.ntp.org"
#define NODE_TIME_VALID_AFTER_MS        1704067200000ULL

#define RSSI_SAMPLE_INTERVAL_MS         200
#define RSSI_PUBLISH_PERIOD_MS          1000
#define RSSI_FILTER_WINDOW              5
#define RSSI_SCAN_ACTIVE_MIN_MS         40
#define RSSI_SCAN_ACTIVE_MAX_MS         80
#define ESPNOW_SEND_TIMEOUT_MS          300
#define ESPNOW_TX_SLOT_MS               120

#define RSSI_VALID_MIN_DBM              (-110)
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

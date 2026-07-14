#include "rssi_measure.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "node_config.h"

static const char *TAG = "rssi_measure";
static const uint8_t k_target_bssid[6] = TARGET_AP_BSSID;
static const uint8_t k_target_ssid[] = TARGET_AP_SSID;

esp_err_t rssi_measure_init_wifi(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "set channel failed");

    return ESP_OK;
}

esp_err_t rssi_measure_scan_target(int8_t *out_rssi_dbm)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = TARGET_AP_USE_BSSID ? (uint8_t *)k_target_bssid : NULL,
        .channel = TARGET_AP_CHANNEL,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = RSSI_SCAN_ACTIVE_MIN_MS,
        .scan_time.active.max = RSSI_SCAN_ACTIVE_MAX_MS,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "get ap num failed");
    if (ap_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t records[16];
    uint16_t record_count = (ap_count > 16) ? 16 : ap_count;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&record_count, records), TAG, "get ap records failed");

    int found = 0;
    int8_t strongest_rssi = -127;
    for (uint16_t i = 0; i < record_count; ++i) {
#if TARGET_AP_USE_BSSID
        if (memcmp(records[i].bssid, k_target_bssid, sizeof(k_target_bssid)) == 0) {
            *out_rssi_dbm = records[i].rssi;
            (void)esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
            return ESP_OK;
        }
#else
        if (strcmp((const char *)records[i].ssid, (const char *)k_target_ssid) == 0) {
            if (!found || records[i].rssi > strongest_rssi) {
                strongest_rssi = records[i].rssi;
                found = 1;
            }
        }
#endif
    }

#if !TARGET_AP_USE_BSSID
    if (found) {
        *out_rssi_dbm = strongest_rssi;
        (void)esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
        return ESP_OK;
    }
#endif

#if RSSI_SCAN_DEBUG_LOG
    ESP_LOGW(TAG,
             "target not found: mode=%s target_ssid=%s records=%u",
             TARGET_AP_USE_BSSID ? "bssid" : "ssid",
             TARGET_AP_USE_BSSID ? "(bssid-mode)" : TARGET_AP_SSID,
             record_count);
    for (uint16_t i = 0; i < record_count; ++i) {
        ESP_LOGW(TAG,
                 "scan[%u] ssid=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X channel=%u rssi=%d",
                 i,
                 (const char *)records[i].ssid,
                 records[i].bssid[0],
                 records[i].bssid[1],
                 records[i].bssid[2],
                 records[i].bssid[3],
                 records[i].bssid[4],
                 records[i].bssid[5],
                 records[i].primary,
                 records[i].rssi);
    }
#endif

    (void)esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    return ESP_ERR_NOT_FOUND;
}

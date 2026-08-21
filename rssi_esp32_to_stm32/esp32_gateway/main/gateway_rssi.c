#include "gateway_rssi.h"

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "gateway_config.h"

static const char *TAG = "gateway_rssi";
#if TARGET_AP_USE_BSSID
static const uint8_t k_target_bssid[6] = TARGET_AP_BSSID;
#else
static const uint8_t k_target_ssid[] = TARGET_AP_SSID;
#endif

static int16_t s_samples[GATEWAY_RSSI_FILTER_WINDOW];
static uint8_t s_sample_count;
static uint8_t s_write_index;
static int8_t s_latest_raw;

static int is_valid_rssi(int8_t raw_dbm)
{
    return raw_dbm >= GATEWAY_RSSI_VALID_MIN_DBM && raw_dbm <= GATEWAY_RSSI_VALID_MAX_DBM;
}

static void filter_add(int8_t raw_dbm)
{
    s_samples[s_write_index] = raw_dbm;
    s_write_index = (uint8_t)((s_write_index + 1u) % GATEWAY_RSSI_FILTER_WINDOW);
    if (s_sample_count < GATEWAY_RSSI_FILTER_WINDOW) {
        s_sample_count++;
    }
    s_latest_raw = raw_dbm;
}

static int filter_average_x10(int16_t *out_x10, uint8_t *out_count)
{
    if (s_sample_count == 0u) {
        return 0;
    }

    int32_t sum_x10 = 0;
    for (uint8_t i = 0; i < s_sample_count; ++i) {
        sum_x10 += (int32_t)s_samples[i] * 10;
    }

    *out_x10 = (int16_t)(sum_x10 / s_sample_count);
    *out_count = s_sample_count;
    return 1;
}

void gateway_rssi_filter_reset(void)
{
    memset(s_samples, 0, sizeof(s_samples));
    s_sample_count = 0;
    s_write_index = 0;
    s_latest_raw = 0;
}

esp_err_t gateway_rssi_measure_once(gateway_rssi_sample_t *out_sample)
{
    if (out_sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t flags = RSSI_ERR_NONE;
    int8_t raw = s_latest_raw;
    uint8_t matched_channel = 0;
    uint8_t matched_bssid[6] = {0};
    esp_err_t err = ESP_OK;

#if GATEWAY_TIME_SYNC_ENABLE
    /* The gateway is already associated with the target AP for SNTP. */
    wifi_ap_record_t associated_ap = {0};
    err = esp_wifi_sta_get_ap_info(&associated_ap);
    if (err == ESP_OK) {
        int found = 0;
#if TARGET_AP_USE_BSSID
        found = memcmp(associated_ap.bssid,
                       k_target_bssid,
                       sizeof(k_target_bssid)) == 0;
#else
        found = strcmp((const char *)associated_ap.ssid,
                       (const char *)k_target_ssid) == 0;
#endif
        found = found && associated_ap.primary == TARGET_AP_CHANNEL;
        if (found) {
            raw = associated_ap.rssi;
            matched_channel = associated_ap.primary;
            memcpy(matched_bssid, associated_ap.bssid, sizeof(matched_bssid));
        } else {
            flags |= RSSI_ERR_AP_NOT_FOUND;
        }
    } else {
        flags |= RSSI_ERR_SCAN_FAILED;
    }
#else

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = TARGET_AP_USE_BSSID ? (uint8_t *)k_target_bssid : NULL,
        .channel = TARGET_AP_CHANNEL,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = GATEWAY_SCAN_ACTIVE_MIN_MS,
        .scan_time.active.max = GATEWAY_SCAN_ACTIVE_MAX_MS,
    };

    err = esp_wifi_scan_start(&scan_config, true);
    if (err == ESP_OK) {
        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err == ESP_OK && ap_count > 0u) {
            wifi_ap_record_t records[16];
            uint16_t record_count = ap_count > 16u ? 16u : ap_count;
            err = esp_wifi_scan_get_ap_records(&record_count, records);
            if (err == ESP_OK) {
                int found = 0;
                for (uint16_t i = 0; i < record_count; ++i) {
#if TARGET_AP_USE_BSSID
                    if (memcmp(records[i].bssid, k_target_bssid, sizeof(k_target_bssid)) == 0) {
                        raw = records[i].rssi;
                        matched_channel = records[i].primary;
                        memcpy(matched_bssid, records[i].bssid, sizeof(matched_bssid));
                        found = 1;
                        break;
                    }
#else
                    if (strcmp((const char *)records[i].ssid, (const char *)k_target_ssid) == 0) {
                        if (!found || records[i].rssi > raw) {
                            raw = records[i].rssi;
                            matched_channel = records[i].primary;
                            memcpy(matched_bssid, records[i].bssid, sizeof(matched_bssid));
                            found = 1;
                        }
                    }
#endif
                }
                if (!found) {
                    flags |= RSSI_ERR_AP_NOT_FOUND;
#if GATEWAY_SCAN_DEBUG_LOG
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
                }
            } else {
                flags |= RSSI_ERR_SCAN_FAILED;
            }
        } else if (err == ESP_OK) {
            flags |= RSSI_ERR_AP_NOT_FOUND;
        } else {
            flags |= RSSI_ERR_SCAN_FAILED;
        }
    } else {
        flags |= RSSI_ERR_SCAN_FAILED;
    }

    (void)esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

    if ((flags & (RSSI_ERR_AP_NOT_FOUND | RSSI_ERR_SCAN_FAILED)) == 0u && is_valid_rssi(raw)) {
        filter_add(raw);
        ESP_LOGI(TAG,
                 "matched ssid=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X channel=%u rssi=%d",
                 TARGET_AP_USE_BSSID ? "(bssid-mode)" : TARGET_AP_SSID,
                 matched_bssid[0],
                 matched_bssid[1],
                 matched_bssid[2],
                 matched_bssid[3],
                 matched_bssid[4],
                 matched_bssid[5],
                 matched_channel,
                 raw);
    }

    int16_t filtered_x10 = 0;
    uint8_t sample_count = 0;
    if (!filter_average_x10(&filtered_x10, &sample_count)) {
        flags |= RSSI_ERR_FILTER_EMPTY;
    }

    out_sample->raw_dbm = raw;
    out_sample->filtered_x10 = filtered_x10;
    out_sample->sample_count = sample_count;
    out_sample->error_flags = flags;
    return err == ESP_OK ? ESP_OK : err;
}

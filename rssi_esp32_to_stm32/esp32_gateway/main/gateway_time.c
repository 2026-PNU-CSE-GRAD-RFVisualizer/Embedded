#include "gateway_time.h"

#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

#include "gateway_config.h"

static const char *TAG = "gateway_time";

#if GATEWAY_TIME_SYNC_ENABLE
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        (void)esp_wifi_connect();
    }
}
#endif

uint64_t gateway_time_now_ms(void)
{
    struct timeval now;
    if (gettimeofday(&now, NULL) != 0) {
        return 0;
    }

    uint64_t timestamp_ms = (uint64_t)now.tv_sec * 1000ULL +
                            (uint64_t)now.tv_usec / 1000ULL;
    return timestamp_ms >= GATEWAY_TIME_VALID_AFTER_MS ? timestamp_ms : 0;
}

bool gateway_time_is_valid(void)
{
    return gateway_time_now_ms() != 0;
}

esp_err_t gateway_time_sync_start(void)
{
#if !GATEWAY_TIME_SYNC_ENABLE
    ESP_LOGW(TAG, "time synchronization disabled");
    return ESP_OK;
#else
    wifi_config_t wifi_config = {0};
    const uint8_t target_bssid[6] = TARGET_AP_BSSID;

    strlcpy((char *)wifi_config.sta.ssid,
            GATEWAY_TIME_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            GATEWAY_TIME_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.channel = ESPNOW_WIFI_CHANNEL;
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
#if TARGET_AP_USE_BSSID
    wifi_config.sta.bssid_set = true;
    memcpy(wifi_config.sta.bssid, target_bssid, sizeof(target_bssid));
#endif

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT,
                                   ESP_EVENT_ANY_ID,
                                   wifi_event_handler,
                                   NULL),
        TAG,
        "register Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                        TAG,
                        "set time-sync Wi-Fi config failed");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, GATEWAY_TIME_SNTP_SERVER);
    esp_sntp_init();

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        return err;
    }

    ESP_LOGI(TAG, "SNTP started; timestamp remains 0 until synchronization completes");
    return ESP_OK;
#endif
}

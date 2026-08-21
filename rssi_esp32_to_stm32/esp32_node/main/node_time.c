#include "node_time.h"

#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

#include "node_config.h"

static const char *TAG = "node_time";

#if NODE_TIME_SYNC_ENABLE
static void time_sync_notification_cb(struct timeval *tv)
{
    uint64_t timestamp_ms = (uint64_t)tv->tv_sec * 1000ULL +
                            (uint64_t)tv->tv_usec / 1000ULL;
    ESP_LOGI(TAG, "SNTP synchronized: timestamp_ms=%llu", timestamp_ms);
}

static void start_sntp_after_ip(void)
{
    if (esp_sntp_enabled()) {
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NODE_TIME_SNTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started after DHCP; server=%s", NODE_TIME_SNTP_SERVER);
}

static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        if (got_ip != NULL) {
            ESP_LOGI(TAG,
                     "DHCP lease acquired: ip=" IPSTR " gateway=" IPSTR,
                     IP2STR(&got_ip->ip_info.ip),
                     IP2STR(&got_ip->ip_info.gw));
            start_sntp_after_ip();
        }
    }
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        ESP_LOGW(TAG,
                 "Wi-Fi disconnected (reason=%u); reconnecting",
                 disconnected != NULL ? disconnected->reason : 0u);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "Wi-Fi reconnect failed: %s", esp_err_to_name(err));
        }
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *connected = event_data;
        ESP_LOGI(TAG,
                 "Wi-Fi associated: channel=%u",
                 connected != NULL ? connected->channel : 0u);
    }
}
#endif

uint64_t node_time_now_ms(void)
{
    struct timeval now;
    if (gettimeofday(&now, NULL) != 0) {
        return 0;
    }

    uint64_t timestamp_ms = (uint64_t)now.tv_sec * 1000ULL +
                            (uint64_t)now.tv_usec / 1000ULL;
    return timestamp_ms >= NODE_TIME_VALID_AFTER_MS ? timestamp_ms : 0;
}

bool node_time_is_valid(void)
{
    return node_time_now_ms() != 0;
}

esp_err_t node_time_sync_start(void)
{
#if !NODE_TIME_SYNC_ENABLE
    ESP_LOGW(TAG, "time synchronization disabled");
    return ESP_OK;
#else
    wifi_config_t wifi_config = {0};
    const uint8_t target_bssid[6] = TARGET_AP_BSSID;

    strlcpy((char *)wifi_config.sta.ssid,
            NODE_TIME_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            NODE_TIME_WIFI_PASSWORD,
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
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT,
                                   IP_EVENT_STA_GOT_IP,
                                   ip_event_handler,
                                   NULL),
        TAG,
        "register IP event handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                        TAG,
                        "set time-sync Wi-Fi config failed");

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        return err;
    }

    ESP_LOGI(TAG, "Wi-Fi time sync requested; SNTP will start after DHCP");
    return ESP_OK;
#endif
}

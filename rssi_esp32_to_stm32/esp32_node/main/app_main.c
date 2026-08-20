#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "espnow_packet.h"
#include "node_config.h"
#include "node_time.h"
#include "rssi_filter.h"
#include "rssi_measure.h"

typedef struct {
    int8_t raw_dbm;
    int16_t filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
    uint64_t measurement_timestamp_ms;
} latest_rssi_t;

static const char *TAG = "rssi_node";
static const uint8_t k_gateway_mac[6] = GATEWAY_ESPNOW_MAC;
static const uint8_t k_target_bssid[6] = TARGET_AP_BSSID;

static SemaphoreHandle_t s_latest_mutex;
static SemaphoreHandle_t s_radio_mutex;
static SemaphoreHandle_t s_send_done_sem;
static latest_rssi_t s_latest = {
    .raw_dbm = 0,
    .filtered_x10 = 0,
    .sample_count = 0,
    .error_flags = RSSI_ERR_FILTER_EMPTY,
};
static uint32_t s_send_ok_count;
static uint32_t s_send_fail_count;

static void mark_send_failure(void)
{
    s_send_fail_count++;
    xSemaphoreTake(s_latest_mutex, portMAX_DELAY);
    s_latest.error_flags |= RSSI_ERR_ESPNOW_SEND_FAIL;
    xSemaphoreGive(s_latest_mutex);
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_send_ok_count++;
    } else {
        mark_send_failure();
    }

    if (s_send_done_sem != NULL) {
        xSemaphoreGive(s_send_done_sem);
    }
}

static esp_err_t espnow_init_sender(void)
{
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(espnow_send_cb), TAG, "send cb failed");

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, k_gateway_mac, sizeof(peer.peer_addr));
    peer.channel = ESPNOW_WIFI_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(k_gateway_mac)) {
        ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer failed");
    }
    return ESP_OK;
}

static void rssi_measure_task(void *arg)
{
    (void)arg;
    rssi_filter_t filter;
    rssi_filter_init(&filter);

    while (true) {
        int8_t raw = 0;
        uint16_t flags = RSSI_ERR_NONE;

        xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
        esp_err_t err = rssi_measure_scan_target(&raw);
        xSemaphoreGive(s_radio_mutex);

        if (err == ESP_OK) {
            if (!rssi_filter_add(&filter, raw)) {
                flags |= RSSI_ERR_FILTER_EMPTY;
            }
        } else if (err == ESP_ERR_NOT_FOUND) {
            flags |= RSSI_ERR_AP_NOT_FOUND;
        } else {
            flags |= RSSI_ERR_SCAN_FAILED;
        }

        int16_t avg_x10 = 0;
        uint8_t sample_count = 0;
        if (!rssi_filter_get_average_x10(&filter, &avg_x10, &sample_count)) {
            flags |= RSSI_ERR_FILTER_EMPTY;
        }
        if (esp_get_free_heap_size() < 20000) {
            flags |= RSSI_ERR_LOW_HEAP;
        }

        uint64_t measurement_timestamp_ms = node_time_now_ms();
        if (measurement_timestamp_ms == 0) {
            flags |= RSSI_ERR_TIME_INVALID;
        }

        xSemaphoreTake(s_latest_mutex, portMAX_DELAY);
        if (err == ESP_OK) {
            s_latest.raw_dbm = raw;
        }
        s_latest.filtered_x10 = avg_x10;
        s_latest.sample_count = sample_count;
        s_latest.error_flags = flags;
        s_latest.measurement_timestamp_ms = measurement_timestamp_ms;
        xSemaphoreGive(s_latest_mutex);

        ESP_LOGI(TAG, "raw=%d filtered_x10=%d count=%u flags=0x%04X",
                 s_latest.raw_dbm, s_latest.filtered_x10, s_latest.sample_count, s_latest.error_flags);

        vTaskDelay(pdMS_TO_TICKS(RSSI_SAMPLE_INTERVAL_MS));
    }
}

static void espnow_tx_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0;

    vTaskDelay(pdMS_TO_TICKS((uint32_t)NODE_ID * ESPNOW_TX_SLOT_MS));

    while (true) {
        latest_rssi_t latest;
        xSemaphoreTake(s_latest_mutex, portMAX_DELAY);
        latest = s_latest;
        xSemaphoreGive(s_latest_mutex);

        rssi_node_packet_t packet = {0};
        packet.node_id = NODE_ID;
        packet.seq = ++seq;
        packet.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        packet.measurement_timestamp_ms = latest.measurement_timestamp_ms;
        memcpy(packet.ap_bssid, k_target_bssid, sizeof(packet.ap_bssid));
        packet.rssi_raw_dbm = latest.raw_dbm;
        packet.rssi_filtered_x10 = latest.filtered_x10;
        packet.sample_count = latest.sample_count;
        packet.error_flags = latest.error_flags;
        rssi_packet_finalize(&packet);

        xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
        while (xSemaphoreTake(s_send_done_sem, 0) == pdTRUE) {
        }

        esp_err_t err = esp_now_send(k_gateway_mac, (const uint8_t *)&packet, sizeof(packet));
        if (err != ESP_OK) {
            mark_send_failure();
            ESP_LOGW(TAG, "esp-now send failed: %s", esp_err_to_name(err));
        } else if (xSemaphoreTake(s_send_done_sem, pdMS_TO_TICKS(ESPNOW_SEND_TIMEOUT_MS)) != pdTRUE) {
            mark_send_failure();
            ESP_LOGW(TAG, "esp-now send callback timeout");
        }
        xSemaphoreGive(s_radio_mutex);

        vTaskDelay(pdMS_TO_TICKS(RSSI_PUBLISH_PERIOD_MS));
    }
}

static void health_task(void *arg)
{
    (void)arg;
    while (true) {
        ESP_LOGI(TAG, "node=%u heap=%lu send_ok=%lu send_fail=%lu",
                 NODE_ID,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)s_send_ok_count,
                 (unsigned long)s_send_fail_count);
        if (!node_time_is_valid()) {
            ESP_LOGW(TAG, "Unix time is not synchronized; timestamp=0 and TIME_INVALID are emitted");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(rssi_measure_init_wifi());
    ESP_ERROR_CHECK(node_time_sync_start());

    s_latest_mutex = xSemaphoreCreateMutex();
    s_radio_mutex = xSemaphoreCreateMutex();
    s_send_done_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK((s_latest_mutex == NULL ||
                     s_radio_mutex == NULL ||
                     s_send_done_sem == NULL)
                        ? ESP_FAIL
                        : ESP_OK);

    ESP_ERROR_CHECK(espnow_init_sender());

    xTaskCreate(rssi_measure_task, "rssi_measure", 4096, NULL, 5, NULL);
    xTaskCreate(espnow_tx_task, "espnow_tx", 4096, NULL, 4, NULL);
    xTaskCreate(health_task, "health", 3072, NULL, 2, NULL);
}

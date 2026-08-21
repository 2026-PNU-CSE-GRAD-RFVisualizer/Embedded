#include "espnow_receiver.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "espnow_packet.h"
#include "gateway_config.h"

static const char *TAG = "espnow_rx";
static QueueHandle_t s_rx_queue;
static gateway_stats_t *s_stats;

uint32_t rssi_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

bool rssi_packet_validate(const rssi_node_packet_t *packet)
{
    if (packet == NULL ||
        packet->magic != RSSI_PACKET_MAGIC ||
        packet->version != RSSI_PACKET_VERSION ||
        packet->payload_len != sizeof(*packet)) {
        return false;
    }

    rssi_node_packet_t copy;
    memcpy(&copy, packet, sizeof(copy));
    uint32_t expected = copy.crc32;
    copy.crc32 = 0;
    return rssi_crc32(&copy, sizeof(copy)) == expected;
}

static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (data == NULL || recv_info == NULL || len != sizeof(rssi_node_packet_t)) {
        if (s_stats != NULL) {
            s_stats->crc_error_count++;
        }
        return;
    }

    espnow_rx_item_t item = {0};
    memcpy(item.mac, recv_info->src_addr, sizeof(item.mac));
    memcpy(&item.packet, data, sizeof(item.packet));

    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        if (s_stats != NULL) {
            s_stats->queue_drop_count++;
        }
    }
}

esp_err_t espnow_receiver_init(QueueHandle_t *out_rx_queue, gateway_stats_t *stats)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "create default Wi-Fi STA netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "set channel");

    s_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_LEN, sizeof(espnow_rx_item_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_stats = stats;
    *out_rx_queue = s_rx_queue;

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(recv_cb), TAG, "recv cb");

    return ESP_OK;
}

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "espnow_packet.h"
#include "espnow_receiver.h"
#include "gateway_config.h"
#include "gateway_rssi.h"
#include "gateway_time.h"
#include "line_protocol.h"
#include "uart_forwarder.h"

typedef struct {
    char text[UART_LINE_MAX_LEN];
} uart_line_t;

typedef struct {
    bool active;
    uint8_t node_id;
    uint32_t last_seq;
    uint32_t last_rx_uptime_ms;
    int8_t last_raw_rssi;
    int16_t last_filtered_x10;
    uint32_t packet_count;
    uint32_t duplicate_count;
    uint32_t lost_count;
    uint16_t last_error_flags;
} node_state_t;

static const char *TAG = "gateway";
static QueueHandle_t s_rx_queue;
static QueueHandle_t s_uart_queue;
static SemaphoreHandle_t s_nodes_mutex;
static gateway_stats_t s_stats;
static node_state_t s_nodes[MAX_NODES];

static void forward_packet_to_stm32(const rssi_node_packet_t *packet)
{
    uart_line_t line = {0};
    if (line_protocol_make_rssi_line(line.text, sizeof(line.text), packet) > 0) {
        if (xQueueSend(s_uart_queue, &line, pdMS_TO_TICKS(20)) != pdTRUE) {
            s_stats.queue_drop_count++;
        }
    }
}

static node_state_t *find_or_alloc_node(uint8_t node_id)
{
    for (size_t i = 0; i < MAX_NODES; ++i) {
        if (s_nodes[i].active && s_nodes[i].node_id == node_id) {
            return &s_nodes[i];
        }
    }

    for (size_t i = 0; i < MAX_NODES; ++i) {
        if (!s_nodes[i].active) {
            memset(&s_nodes[i], 0, sizeof(s_nodes[i]));
            s_nodes[i].active = true;
            s_nodes[i].node_id = node_id;
            return &s_nodes[i];
        }
    }

    return NULL;
}

static void update_node_state(const rssi_node_packet_t *packet)
{
    xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
    node_state_t *node = find_or_alloc_node(packet->node_id);
    if (node == NULL) {
        xSemaphoreGive(s_nodes_mutex);
        ESP_LOGW(TAG, "node table full, dropping node=%u", packet->node_id);
        return;
    }

    if (node->packet_count > 0) {
        if (packet->seq == node->last_seq) {
            node->duplicate_count++;
        } else if (packet->seq > node->last_seq + 1u) {
            node->lost_count += packet->seq - node->last_seq - 1u;
        }
    }

    node->last_seq = packet->seq;
    node->last_rx_uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    node->last_raw_rssi = packet->rssi_raw_dbm;
    node->last_filtered_x10 = packet->rssi_filtered_x10;
    node->last_error_flags = packet->error_flags;
    node->packet_count++;
    xSemaphoreGive(s_nodes_mutex);
}

static void gateway_process_task(void *arg)
{
    (void)arg;
    espnow_rx_item_t item;

    while (true) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!rssi_packet_validate(&item.packet)) {
            s_stats.crc_error_count++;
            ESP_LOGW(TAG, "invalid packet crc");
            continue;
        }

        s_stats.rx_count++;
        update_node_state(&item.packet);

        forward_packet_to_stm32(&item.packet);
    }
}

static void gateway_status_task(void *arg)
{
    (void)arg;
    while (true) {
#if GATEWAY_STATUS_LINE_ENABLE
        uart_line_t line = {0};
        uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (line_protocol_make_gwstat_line(line.text,
                                           sizeof(line.text),
                                           uptime_ms,
                                           s_stats.rx_count,
                                           s_stats.crc_error_count,
                                           s_stats.queue_drop_count) > 0) {
            (void)xQueueSend(s_uart_queue, &line, pdMS_TO_TICKS(20));
        }
#endif

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        xSemaphoreTake(s_nodes_mutex, portMAX_DELAY);
        for (size_t i = 0; i < MAX_NODES; ++i) {
            if (s_nodes[i].active) {
                uint32_t age_ms = now_ms - s_nodes[i].last_rx_uptime_ms;
                if (age_ms > GATEWAY_NODE_TIMEOUT_MS) {
                    ESP_LOGW(TAG,
                             "node=%u offline: no packet for %lu ms; removing cached state",
                             s_nodes[i].node_id,
                             (unsigned long)age_ms);
                    memset(&s_nodes[i], 0, sizeof(s_nodes[i]));
                    continue;
                }

                ESP_LOGI(TAG,
                         "node=%u seq=%lu raw=%d filt_x10=%d count=%lu dup=%lu lost=%lu err=0x%04X",
                         s_nodes[i].node_id,
                         (unsigned long)s_nodes[i].last_seq,
                         s_nodes[i].last_raw_rssi,
                         s_nodes[i].last_filtered_x10,
                         (unsigned long)s_nodes[i].packet_count,
                         (unsigned long)s_nodes[i].duplicate_count,
                         (unsigned long)s_nodes[i].lost_count,
                         s_nodes[i].last_error_flags);
            }
        }
        xSemaphoreGive(s_nodes_mutex);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#if GATEWAY_FAKE_RSSI_TEST
static void fake_rssi_task(void *arg)
{
    (void)arg;
    uint32_t seq[GATEWAY_FAKE_NODE_COUNT] = {0};
    uint8_t node_index = 0;

    while (true) {
        uint8_t node_id = (uint8_t)(node_index + 1u);
        int8_t raw = (int8_t)(-50 - (int8_t)(node_index * 4u));
        int16_t filtered_x10 = (int16_t)(raw * 10 - (int16_t)(node_index * 2u));

        rssi_node_packet_t packet = {0};
        packet.node_id = node_id;
        packet.seq = ++seq[node_index];
        packet.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        packet.measurement_timestamp_ms = gateway_time_now_ms();
        packet.rssi_raw_dbm = raw;
        packet.rssi_filtered_x10 = filtered_x10;
        packet.sample_count = 5;
        packet.error_flags = packet.measurement_timestamp_ms == 0
                                 ? RSSI_ERR_TIME_INVALID
                                 : RSSI_ERR_NONE;

        s_stats.rx_count++;
        update_node_state(&packet);

        forward_packet_to_stm32(&packet);

        node_index = (uint8_t)((node_index + 1u) % GATEWAY_FAKE_NODE_COUNT);
        vTaskDelay(pdMS_TO_TICKS(GATEWAY_FAKE_PERIOD_MS));
    }
}
#endif

#if GATEWAY_LOCAL_RSSI_ENABLE
static void gateway_local_rssi_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0;

    gateway_rssi_filter_reset();

    while (true) {
        gateway_rssi_sample_t sample = {0};
        (void)gateway_rssi_measure_once(&sample);

        rssi_node_packet_t packet = {0};
        packet.node_id = GATEWAY_LOCAL_NODE_ID;
        packet.seq = ++seq;
        packet.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        packet.measurement_timestamp_ms = gateway_time_now_ms();
        packet.rssi_raw_dbm = sample.raw_dbm;
        packet.rssi_filtered_x10 = sample.filtered_x10;
        packet.sample_count = sample.sample_count;
        packet.error_flags = sample.error_flags;
        if (packet.measurement_timestamp_ms == 0) {
            packet.error_flags |= RSSI_ERR_TIME_INVALID;
        }

        s_stats.rx_count++;
        update_node_state(&packet);
        forward_packet_to_stm32(&packet);

        ESP_LOGI(TAG,
                 "local node=%u seq=%lu raw=%d filt_x10=%d count=%u err=0x%04X",
                 GATEWAY_LOCAL_NODE_ID,
                 (unsigned long)seq,
                 sample.raw_dbm,
                 sample.filtered_x10,
                 sample.sample_count,
                 sample.error_flags);

        vTaskDelay(pdMS_TO_TICKS(GATEWAY_LOCAL_RSSI_PERIOD_MS));
    }
}
#endif

void app_main(void)
{
    s_nodes_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_nodes_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    ESP_ERROR_CHECK(uart_forwarder_init(&s_uart_queue));
    ESP_ERROR_CHECK(espnow_receiver_init(&s_rx_queue, &s_stats));
    ESP_ERROR_CHECK(gateway_time_sync_start());

    xTaskCreate(uart_forwarder_task, "uart_forwarder", 4096, NULL, 5, NULL);
#if GATEWAY_FAKE_RSSI_TEST
    ESP_LOGW(TAG, "GATEWAY_FAKE_RSSI_TEST enabled: synthetic remote node data is generated");
    xTaskCreate(fake_rssi_task, "fake_rssi", 4096, NULL, 4, NULL);
#else
    xTaskCreate(gateway_process_task, "gateway_process", 4096, NULL, 4, NULL);
#endif
#if GATEWAY_LOCAL_RSSI_ENABLE
    ESP_LOGI(TAG, "gateway local RSSI enabled as node_id=%u", GATEWAY_LOCAL_NODE_ID);
    xTaskCreate(gateway_local_rssi_task, "gateway_local_rssi", 4096, NULL, 3, NULL);
#endif
    xTaskCreate(gateway_status_task, "gateway_status", 4096, NULL, 2, NULL);
}

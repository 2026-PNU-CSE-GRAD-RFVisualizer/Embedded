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
#include "line_protocol.h"
#include "uart_forwarder.h"

typedef struct {
    char text[UART_LINE_MAX_LEN];
} uart_line_t;

typedef struct {
    bool active;
    uint8_t node_id;
    uint32_t last_seq;
    uint32_t last_node_uptime_ms;
    uint32_t last_gateway_rx_ms;
    int8_t last_raw_rssi;
    int16_t last_filtered_x10;
    uint32_t packet_rx_count;
    uint32_t accepted_count;
    uint32_t duplicate_count;
    uint32_t lost_count;
    uint32_t out_of_order_count;
    uint32_t reboot_count;
    uint16_t last_error_flags;
} node_state_t;

static const char *TAG = "gateway";
static QueueHandle_t s_rx_queue;
static QueueHandle_t s_uart_queue;
static SemaphoreHandle_t s_node_mutex;
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

static bool update_node_state(const rssi_node_packet_t *packet)
{
    if (packet == NULL || packet->node_id == 0u || packet->node_id > MAX_NODES) {
        ESP_LOGW(TAG, "invalid node id");
        return false;
    }

    /* 원격 노드 Task와 로컬 RSSI Task가 같은 테이블을 갱신하므로 직렬화한다. */
    xSemaphoreTake(s_node_mutex, portMAX_DELAY);
    node_state_t *node = find_or_alloc_node(packet->node_id);
    if (node == NULL) {
        xSemaphoreGive(s_node_mutex);
        ESP_LOGW(TAG, "node table full, dropping node=%u", packet->node_id);
        return false;
    }

    bool had_packet = node->packet_rx_count > 0u;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t receive_gap_ms = had_packet ? now_ms - node->last_gateway_rx_ms : 0u;
    bool rebooted = false;
    node->packet_rx_count++;
    node->last_gateway_rx_ms = now_ms;

    if (had_packet) {
        int32_t seq_delta = (int32_t)(packet->seq - node->last_seq);
        if (seq_delta <= 0 && packet->uptime_ms < node->last_node_uptime_ms) {
            uint32_t uptime_rollback = node->last_node_uptime_ms - packet->uptime_ms;
            /* Boot ID가 없으므로 uptime 롤백과 초기 seq/수신 공백을 조합해 재부팅을 추정한다. */
            rebooted = uptime_rollback > NODE_REBOOT_ROLLBACK_MIN_MS &&
                       (packet->seq <= NODE_REBOOT_INITIAL_SEQ_MAX ||
                        receive_gap_ms > NODE_COMMUNICATION_TIMEOUT_MS);
        }

        if (!rebooted) {
            if (seq_delta == 0) {
                /* 중복 패킷은 UART 대역폭을 쓰지 않도록 STM32 전달 전에 차단한다. */
                node->duplicate_count++;
                xSemaphoreGive(s_node_mutex);
                return false;
            }
            if (seq_delta < 0) {
                /* 과거 패킷이 Gateway와 STM32의 최신 상태를 되돌리지 못하게 한다. */
                node->out_of_order_count++;
                xSemaphoreGive(s_node_mutex);
                return false;
            }
            if (seq_delta > 1) {
                node->lost_count += (uint32_t)(seq_delta - 1);
            }
        } else {
            node->reboot_count++;
        }
    }

    node->last_seq = packet->seq;
    node->last_node_uptime_ms = packet->uptime_ms;
    node->last_raw_rssi = packet->rssi_raw_dbm;
    node->last_filtered_x10 = packet->rssi_filtered_x10;
    node->last_error_flags = packet->error_flags;
    node->accepted_count++;
    xSemaphoreGive(s_node_mutex);
    return true;
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
        if (update_node_state(&item.packet)) {
            forward_packet_to_stm32(&item.packet);
        }
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

        /* 로그 출력 중 갱신 Task를 오래 막지 않도록 테이블 복사본을 만든다. */
        node_state_t snapshot[MAX_NODES];
        xSemaphoreTake(s_node_mutex, portMAX_DELAY);
        memcpy(snapshot, s_nodes, sizeof(snapshot));
        xSemaphoreGive(s_node_mutex);

        for (size_t i = 0; i < MAX_NODES; ++i) {
            if (snapshot[i].active) {
                ESP_LOGI(TAG,
                         "node=%u seq=%lu raw=%d filt_x10=%d rx=%lu accepted=%lu dup=%lu ooo=%lu lost=%lu reboot=%lu err=0x%04X",
                         snapshot[i].node_id,
                         (unsigned long)snapshot[i].last_seq,
                         snapshot[i].last_raw_rssi,
                         snapshot[i].last_filtered_x10,
                         (unsigned long)snapshot[i].packet_rx_count,
                         (unsigned long)snapshot[i].accepted_count,
                         (unsigned long)snapshot[i].duplicate_count,
                         (unsigned long)snapshot[i].out_of_order_count,
                         (unsigned long)snapshot[i].lost_count,
                         (unsigned long)snapshot[i].reboot_count,
                         snapshot[i].last_error_flags);
            }
        }
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
        packet.rssi_raw_dbm = raw;
        packet.rssi_filtered_x10 = filtered_x10;
        packet.sample_count = 5;
        packet.error_flags = 0;

        s_stats.rx_count++;
        if (update_node_state(&packet)) {
            forward_packet_to_stm32(&packet);
        }

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
        packet.rssi_raw_dbm = sample.raw_dbm;
        packet.rssi_filtered_x10 = sample.filtered_x10;
        packet.sample_count = sample.sample_count;
        packet.error_flags = sample.error_flags;

        s_stats.rx_count++;
        if (update_node_state(&packet)) {
            forward_packet_to_stm32(&packet);
        }

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
    ESP_ERROR_CHECK(uart_forwarder_init(&s_uart_queue));
    ESP_ERROR_CHECK(espnow_receiver_init(&s_rx_queue, &s_stats));
    s_node_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_node_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK);

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

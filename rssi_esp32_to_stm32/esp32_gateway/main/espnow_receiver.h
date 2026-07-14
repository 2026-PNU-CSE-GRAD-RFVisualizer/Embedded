#ifndef ESPNOW_RECEIVER_H
#define ESPNOW_RECEIVER_H

#include <stdint.h>

#include "esp_err.h"
#include "espnow_packet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint8_t mac[6];
    rssi_node_packet_t packet;
} espnow_rx_item_t;

typedef struct {
    uint32_t rx_count;
    uint32_t crc_error_count;
    uint32_t queue_drop_count;
} gateway_stats_t;

esp_err_t espnow_receiver_init(QueueHandle_t *out_rx_queue, gateway_stats_t *stats);

#endif

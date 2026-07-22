#ifndef RSSI_PREPROCESSOR_H
#define RSSI_PREPROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "rssi_line_parser.h"

#define RSSI_NODE_TIMEOUT_MS             3000u
#define RSSI_VALID_DATA_TIMEOUT_MS       3000u
#define RSSI_REBOOT_ROLLBACK_MIN_MS      3000u
#define RSSI_REBOOT_INITIAL_SEQ_MAX      16u
#define RSSI_VALID_MIN_DBM               (-100)
#define RSSI_VALID_MAX_DBM               (-10)
#define RSSI_VALID_MIN_X10               (-1000)
#define RSSI_VALID_MAX_X10               (-100)
#define RSSI_MAX_SAMPLE_COUNT            32u

#define RSSI_ERR_AP_NOT_FOUND            0x0001u
#define RSSI_ERR_SCAN_FAILED             0x0002u
#define RSSI_ERR_FILTER_EMPTY            0x0004u
#define RSSI_MEASUREMENT_ERROR_MASK      (RSSI_ERR_AP_NOT_FOUND | \
                                          RSSI_ERR_SCAN_FAILED | \
                                          RSSI_ERR_FILTER_EMPTY)

typedef enum {
    RSSI_UPDATE_REJECTED = 0,
    RSSI_UPDATE_ACCEPTED,
    RSSI_UPDATE_INVALID_MEASUREMENT,
    RSSI_UPDATE_DUPLICATE,
    RSSI_UPDATE_OUT_OF_ORDER,
    RSSI_UPDATE_REBOOTED
} rssi_update_result_t;

typedef struct {
    bool active;
    /* 통신 생존 여부와 RSSI 데이터 신선도는 서로 다른 상태다. */
    bool communication_timed_out;
    bool rssi_stale;
    bool has_valid_rssi;
    bool last_packet_measurement_valid;
    uint8_t node_id;
    uint32_t last_seq;
    /* 모든 패킷 수신 시각과 마지막 정상 측정 시각을 분리한다. */
    uint32_t last_packet_rx_ms;
    uint32_t last_valid_update_ms;
    uint32_t node_uptime_ms;
    int8_t last_raw_rssi;
    int16_t last_filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
    /* packet_rx에는 중복·역순도 포함되고 accepted에는 반영된 패킷만 포함된다. */
    uint32_t packet_rx_count;
    uint32_t accepted_count;
    uint32_t valid_count;
    uint32_t invalid_count;
    uint32_t lost_count;
    uint32_t duplicate_count;
    uint32_t out_of_order_count;
    uint32_t reboot_count;
} stm32_node_state_t;

typedef struct {
    stm32_node_state_t nodes[RSSI_MAX_NODES];
    uint32_t checksum_error_count;
    uint32_t format_error_count;
    uint32_t uart_overflow_count;
    uint32_t total_packet_rx_count;
    uint32_t total_accepted_count;
} rssi_preprocessor_t;

void rssi_preprocessor_init(rssi_preprocessor_t *ctx);
rssi_update_result_t rssi_preprocessor_update(rssi_preprocessor_t *ctx,
                                               const rssi_measurement_t *measurement,
                                               uint32_t now_ms);
void rssi_preprocessor_update_timeouts(rssi_preprocessor_t *ctx, uint32_t now_ms);
uint8_t rssi_preprocessor_active_count(const rssi_preprocessor_t *ctx);
bool rssi_preprocessor_node_rssi_valid(const stm32_node_state_t *node);

#endif

#include "rssi_preprocessor.h"

#include <stddef.h>
#include <string.h>

void rssi_preprocessor_init(rssi_preprocessor_t *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

static stm32_node_state_t *find_or_alloc_node(rssi_preprocessor_t *ctx, uint8_t node_id)
{
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        if (ctx->nodes[i].active && ctx->nodes[i].node_id == node_id) {
            return &ctx->nodes[i];
        }
    }

    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        if (!ctx->nodes[i].active) {
            memset(&ctx->nodes[i], 0, sizeof(ctx->nodes[i]));
            ctx->nodes[i].active = true;
            ctx->nodes[i].communication_timed_out = false;
            ctx->nodes[i].rssi_stale = true;
            ctx->nodes[i].node_id = node_id;
            return &ctx->nodes[i];
        }
    }

    return NULL;
}

static bool measurement_values_valid(const rssi_measurement_t *measurement)
{
    /* AP 탐색/스캔 실패 패킷에는 과거 필터값이 들어올 수 있으므로 사용하지 않는다. */
    if ((measurement->error_flags & RSSI_MEASUREMENT_ERROR_MASK) != 0u) {
        return false;
    }

    return measurement->rssi_raw_dbm >= RSSI_VALID_MIN_DBM &&
           measurement->rssi_raw_dbm <= RSSI_VALID_MAX_DBM &&
           measurement->rssi_filtered_x10 >= RSSI_VALID_MIN_X10 &&
           measurement->rssi_filtered_x10 <= RSSI_VALID_MAX_X10 &&
           measurement->sample_count > 0u &&
           measurement->sample_count <= RSSI_MAX_SAMPLE_COUNT;
}

rssi_update_result_t rssi_preprocessor_update(rssi_preprocessor_t *ctx,
                                               const rssi_measurement_t *measurement,
                                               uint32_t now_ms)
{
    if (ctx == NULL || measurement == NULL ||
        measurement->node_id == 0u || measurement->node_id > RSSI_MAX_NODES) {
        if (ctx != NULL) {
            ctx->format_error_count++;
        }
        return RSSI_UPDATE_REJECTED;
    }

    stm32_node_state_t *node = find_or_alloc_node(ctx, measurement->node_id);
    if (node == NULL) {
        return RSSI_UPDATE_REJECTED;
    }

    bool had_packet = node->packet_rx_count > 0u;
    uint32_t receive_gap_ms = had_packet ? now_ms - node->last_packet_rx_ms : 0u;
    bool rebooted = false;

    /* 중복·역순이라도 UART로 수신된 사실은 통신 상태 통계에 기록한다. */
    node->packet_rx_count++;
    ctx->total_packet_rx_count++;
    node->last_packet_rx_ms = now_ms;
    node->communication_timed_out = false;

    if (had_packet) {
        int32_t seq_delta = (int32_t)(measurement->seq - node->last_seq);
        if (seq_delta <= 0 && measurement->uptime_ms < node->node_uptime_ms) {
            uint32_t uptime_rollback = node->node_uptime_ms - measurement->uptime_ms;
            /*
             * Boot ID가 없는 현재 프로토콜에서는 uptime 롤백과 초기 seq/수신 공백을
             * 함께 확인해야 지연 도착 패킷을 재부팅으로 오인할 가능성이 줄어든다.
             */
            rebooted = uptime_rollback > RSSI_REBOOT_ROLLBACK_MIN_MS &&
                       (measurement->seq <= RSSI_REBOOT_INITIAL_SEQ_MAX ||
                        receive_gap_ms > RSSI_NODE_TIMEOUT_MS);
        }

        if (!rebooted) {
            if (seq_delta == 0) {
                /* 중복은 통계만 증가시키고 RSSI/seq/유효 시각을 덮어쓰지 않는다. */
                node->duplicate_count++;
                return RSSI_UPDATE_DUPLICATE;
            }
            if (seq_delta < 0) {
                /* 늦게 도착한 과거 패킷이 최신 상태를 되돌리지 못하게 폐기한다. */
                node->out_of_order_count++;
                return RSSI_UPDATE_OUT_OF_ORDER;
            }
            if (seq_delta > 1) {
                node->lost_count += (uint32_t)(seq_delta - 1);
            }
        } else {
            node->reboot_count++;
        }
    }

    node->last_seq = measurement->seq;
    node->node_uptime_ms = measurement->uptime_ms;
    node->error_flags = measurement->error_flags;
    node->accepted_count++;
    ctx->total_accepted_count++;

    if (!measurement_values_valid(measurement)) {
        /* 기존 정상 RSSI는 보존하되 이번 패킷을 최신 정상 측정으로 표시하지 않는다. */
        node->last_packet_measurement_valid = false;
        node->invalid_count++;
        return RSSI_UPDATE_INVALID_MEASUREMENT;
    }

    node->last_raw_rssi = measurement->rssi_raw_dbm;
    node->last_filtered_x10 = measurement->rssi_filtered_x10;
    node->sample_count = measurement->sample_count;
    node->last_valid_update_ms = now_ms;
    node->last_packet_measurement_valid = true;
    node->has_valid_rssi = true;
    node->rssi_stale = false;
    node->valid_count++;

    return rebooted ? RSSI_UPDATE_REBOOTED : RSSI_UPDATE_ACCEPTED;
}

void rssi_preprocessor_update_timeouts(rssi_preprocessor_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL) {
        return;
    }

    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        stm32_node_state_t *node = &ctx->nodes[i];
        if (!node->active) {
            continue;
        }

        /* 패킷 미수신과 정상 RSSI 미수신을 별도의 Timeout으로 계산한다. */
        node->communication_timed_out =
            (now_ms - node->last_packet_rx_ms) > RSSI_NODE_TIMEOUT_MS;
        node->rssi_stale = !node->has_valid_rssi ||
            (now_ms - node->last_valid_update_ms) > RSSI_VALID_DATA_TIMEOUT_MS;
    }
}

bool rssi_preprocessor_node_rssi_valid(const stm32_node_state_t *node)
{
    /* 서버가 위치 계산에 사용해도 되는 RSSI인지 한 곳에서 일관되게 판정한다. */
    return node != NULL && node->active && node->has_valid_rssi &&
           node->last_packet_measurement_valid && !node->rssi_stale &&
           !node->communication_timed_out;
}

uint8_t rssi_preprocessor_active_count(const rssi_preprocessor_t *ctx)
{
    if (ctx == NULL) {
        return 0u;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        if (ctx->nodes[i].active && !ctx->nodes[i].communication_timed_out) {
            count++;
        }
    }
    return count;
}

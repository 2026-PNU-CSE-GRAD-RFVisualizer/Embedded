#include "rssi_preprocessor.h"

#include <string.h>

void rssi_preprocessor_init(rssi_preprocessor_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
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
            ctx->nodes[i].node_id = node_id;
            return &ctx->nodes[i];
        }
    }

    return NULL;
}

bool rssi_preprocessor_update(rssi_preprocessor_t *ctx,
                              const rssi_measurement_t *measurement,
                              uint32_t now_ms)
{
    stm32_node_state_t *node = find_or_alloc_node(ctx, measurement->node_id);
    if (node == NULL) {
        return false;
    }

    if (node->received_count > 0u) {
        if (measurement->seq == node->last_seq) {
            node->duplicate_count++;
        } else if (measurement->seq > node->last_seq + 1u) {
            node->lost_count += measurement->seq - node->last_seq - 1u;
        }
    }

    node->last_seq = measurement->seq;
    node->last_update_ms = now_ms;
    node->node_uptime_ms = measurement->uptime_ms;
    node->measurement_timestamp_ms = measurement->measurement_timestamp_ms;
    node->last_raw_rssi = measurement->rssi_raw_dbm;
    node->last_filtered_x10 = measurement->rssi_filtered_x10;
    node->sample_count = measurement->sample_count;
    node->error_flags = measurement->error_flags;
    node->timed_out = false;
    node->received_count++;
    ctx->total_received_count++;

    return true;
}

void rssi_preprocessor_update_timeouts(rssi_preprocessor_t *ctx, uint32_t now_ms)
{
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        stm32_node_state_t *node = &ctx->nodes[i];
        if (node->active && (now_ms - node->last_update_ms) > RSSI_NODE_TIMEOUT_MS) {
            node->timed_out = true;
        }
    }
}

uint8_t rssi_preprocessor_active_count(const rssi_preprocessor_t *ctx)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        if (ctx->nodes[i].active && !ctx->nodes[i].timed_out) {
            count++;
        }
    }
    return count;
}

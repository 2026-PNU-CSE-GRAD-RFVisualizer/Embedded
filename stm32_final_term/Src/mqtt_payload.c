#include "mqtt_payload.h"

#include <stdarg.h>
#include <stdio.h>

static int append_text(char *out, size_t out_len, size_t *offset, const char *fmt, ...)
{
    if (*offset >= out_len) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(&out[*offset], out_len - *offset, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= out_len - *offset) {
        return -1;
    }

    *offset += (size_t)written;
    return 0;
}

static int filtered_rssi_dbm(int16_t filtered_x10)
{
    if (filtered_x10 >= 0) {
        return (filtered_x10 + 5) / 10;
    }

    return (filtered_x10 - 5) / 10;
}

static int append_node_id(char *out, size_t out_len, size_t *offset, uint8_t node_id)
{
    if (node_id == 5u) {
        return append_text(out, out_len, offset, "\"gw-01\"");
    }

    return append_text(out, out_len, offset, "\"node-%02u\"", node_id);
}

int mqtt_payload_build_snapshot(char *out,
                                size_t out_len,
                                const char *gateway_id,
                                const rssi_preprocessor_t *ctx,
                                uint32_t now_ms)
{
    if (out == NULL || out_len == 0u || gateway_id == NULL || ctx == NULL) {
        return -1;
    }

    size_t offset = 0;
    if (append_text(out,
                    out_len,
                    &offset,
                    "{\"schema_version\":2,\"gateway_id\":\"%s\",\"timestamp\":%lu,"
                    "\"active_node_count\":%u,\"rx_count\":%lu,\"accepted_count\":%lu,"
                    "\"checksum_errors\":%lu,\"format_errors\":%lu,\"uart_overflows\":%lu,"
                    "\"readings\":[",
                    gateway_id,
                    (unsigned long)now_ms,
                    rssi_preprocessor_active_count(ctx),
                    (unsigned long)ctx->total_packet_rx_count,
                    (unsigned long)ctx->total_accepted_count,
                    (unsigned long)ctx->checksum_error_count,
                    (unsigned long)ctx->format_error_count,
                    (unsigned long)ctx->uart_overflow_count) != 0) {
        return -1;
    }

    uint8_t emitted = 0;
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        const stm32_node_state_t *node = &ctx->nodes[i];
        if (!node->active) {
            continue;
        }

        uint32_t age_ms = now_ms - node->last_packet_rx_ms;
        /* 정상값이 한 번도 없으면 UINT32_MAX를 명시적인 age sentinel로 사용한다. */
        uint32_t valid_age_ms = node->has_valid_rssi
            ? now_ms - node->last_valid_update_ms
            : UINT32_MAX;
        bool valid = rssi_preprocessor_node_rssi_valid(node);

        if (append_text(out,
                        out_len,
                        &offset,
                        "%s{\"node_id\":",
                        emitted > 0u ? "," : "") != 0) {
            return -1;
        }

        if (append_node_id(out, out_len, &offset, node->node_id) != 0) {
            return -1;
        }

        /* 기존 정수 rssi는 유지하고 정밀한 고정소수점 rssi_x10을 함께 제공한다. */
        if (append_text(out,
                        out_len,
                        &offset,
                        ",\"rssi\":%d,\"rssi_x10\":%d,\"rssi_raw\":%d,"
                        "\"seq\":%lu,\"age_ms\":%lu,\"valid_age_ms\":%lu,"
                        "\"valid\":%s,\"timed_out\":%s,\"status\":%u,"
                        "\"rx_count\":%lu,\"accepted_count\":%lu,\"valid_count\":%lu,"
                        "\"invalid_count\":%lu,\"lost_count\":%lu,\"duplicate_count\":%lu,"
                        "\"out_of_order_count\":%lu,\"reboot_count\":%lu}",
                        filtered_rssi_dbm(node->last_filtered_x10),
                        node->last_filtered_x10,
                        node->last_raw_rssi,
                        (unsigned long)node->last_seq,
                        (unsigned long)age_ms,
                        (unsigned long)valid_age_ms,
                        valid ? "true" : "false",
                        node->communication_timed_out ? "true" : "false",
                        node->error_flags,
                        (unsigned long)node->packet_rx_count,
                        (unsigned long)node->accepted_count,
                        (unsigned long)node->valid_count,
                        (unsigned long)node->invalid_count,
                        (unsigned long)node->lost_count,
                        (unsigned long)node->duplicate_count,
                        (unsigned long)node->out_of_order_count,
                        (unsigned long)node->reboot_count) != 0) {
            return -1;
        }
        emitted++;
    }

    if (append_text(out, out_len, &offset, "]}") != 0) {
        return -1;
    }

    return (int)offset;
}
